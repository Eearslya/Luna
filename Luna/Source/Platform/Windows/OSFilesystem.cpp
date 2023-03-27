#include <Luna/Platform/Windows/OSFilesystem.hpp>
#include <Luna/Utility/Log.hpp>

namespace Luna {
FileHandle OSMappedFile::Open(const std::filesystem::path& path, FileMode mode) {
	try {
		return MakeHandle<OSMappedFile>(path, mode);
	} catch (const std::exception& e) {
		Log::Error("OSFilesystem",
		           "Failed to open file '{}' for {}: {}",
		           path.string(),
		           mode == FileMode::ReadOnly ? "reading" : "writing",
		           e.what());

		return {};
	}
}

OSMappedFile::OSMappedFile(const std::filesystem::path& path, FileMode mode) {
	DWORD access      = 0;
	DWORD disposition = 0;

	const auto dir = path.parent_path();

	switch (mode) {
		case FileMode::ReadOnly:
			access      = GENERIC_READ;
			disposition = OPEN_EXISTING;
			break;

		case FileMode::ReadWrite:
			if (!std::filesystem::is_directory(dir) && !std::filesystem::create_directories(dir)) {
				throw std::runtime_error("Could not create directories for file!");
			}
			access      = GENERIC_READ | GENERIC_WRITE;
			disposition = OPEN_ALWAYS;
			break;

		case FileMode::WriteOnly:
		case FileMode::WriteOnlyTransactional:
			if (!std::filesystem::is_directory(dir) && !std::filesystem::create_directories(dir)) {
				throw std::runtime_error("Could not create directories for file!");
			}
			access      = GENERIC_READ | GENERIC_WRITE;
			disposition = CREATE_ALWAYS;
			break;
	}

	if (mode == FileMode::WriteOnlyTransactional) {
		// TODO
	}

	const auto p = _renameFromOnClose.empty() ? path : _renameFromOnClose;
	_file        = CreateFileW(p.wstring().c_str(),
                      access,
                      FILE_SHARE_READ,
                      nullptr,
                      disposition,
                      FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
                      INVALID_HANDLE_VALUE);
	if (_file == INVALID_HANDLE_VALUE) { throw std::runtime_error("Failed to open file!"); }

	if (mode != FileMode::WriteOnly && mode != FileMode::WriteOnlyTransactional) {
		DWORD hi;
		DWORD lo     = GetFileSize(_file, &hi);
		_size        = (uint64_t(hi) << 32) | uint64_t(lo);
		_fileMapping = CreateFileMappingW(_file, nullptr, PAGE_READONLY, 0, 0, nullptr);
	}
}

OSMappedFile::~OSMappedFile() noexcept {
	if (_fileMapping) { CloseHandle(_fileMapping); }
	if (_file != INVALID_HANDLE_VALUE) { CloseHandle(_file); }
}

uint64_t OSMappedFile::GetSize() {
	return _size;
}

struct PageSizeQuery {
	PageSizeQuery() {
		SYSTEM_INFO systemInfo = {};
		GetSystemInfo(&systemInfo);
		PageSize = systemInfo.dwPageSize;
	}
	uint32_t PageSize = 0;
};

FileMappingHandle OSMappedFile::MapSubset(uint64_t offset, size_t range) {
	static const PageSizeQuery pageSizeQuery;

	if (offset + range > _size) { return {}; }
	if (!_fileMapping) { return {}; }

	const uint64_t beginMap   = offset & ~uint64_t(pageSizeQuery.PageSize - 1);
	const DWORD hi            = DWORD(beginMap >> 32);
	const DWORD lo            = DWORD(beginMap & 0xffffffff);
	const uint64_t endMapping = offset + range;
	const size_t mappedSize   = endMapping - beginMap;

	void* mapped = MapViewOfFile(_fileMapping, FILE_MAP_READ, hi, lo, mappedSize);
	if (!mapped) { return {}; }

	return MakeHandle<FileMapping>(
		ReferenceFromThis(), offset, static_cast<uint8_t*>(mapped) + beginMap, mappedSize, offset - beginMap, range);
}

FileMappingHandle OSMappedFile::MapWrite(size_t size) {
	_size          = size;
	const DWORD hi = DWORD(size >> 32);
	const DWORD lo = DWORD(size & 0xffffffff);

	HANDLE fileView = CreateFileMappingW(_file, nullptr, PAGE_READWRITE, hi, lo, nullptr);
	if (!fileView) { return {}; }

	void* mapped = MapViewOfFile(fileView, FILE_MAP_ALL_ACCESS, 0, 0, size);
	CloseHandle(fileView);
	if (!mapped) { return {}; }

	return MakeHandle<FileMapping>(ReferenceFromThis(), 0, static_cast<uint8_t*>(mapped), size, 0, size);
}

void OSMappedFile::Unmap(void* mapped, size_t range) {
	if (mapped) { UnmapViewOfFile(mapped); }
}

OSFilesystem::OSFilesystem(const Path& base) : _basePath(std::string(base)) {}

OSFilesystem::~OSFilesystem() noexcept {
	for (auto& handler : _handlers) {
		CancelIo(handler.second.Handle);
		CloseHandle(handler.second.Handle);
		CloseHandle(handler.second.Event);
	}
}

std::filesystem::path OSFilesystem::GetFilesystemPath(const Path& path) const {
	return _basePath / std::string(path);
}

int OSFilesystem::GetWatchFD() const {
	return -1;
}

std::vector<ListEntry> OSFilesystem::List(const Path& path) {
	const std::filesystem::path base = std::string(path);
	std::vector<ListEntry> entries;
	WIN32_FIND_DATAW result;
	auto p = GetFilesystemPath(path);
	p /= L"*";

	HANDLE handle = FindFirstFileW(p.wstring().c_str(), &result);
	if (handle == INVALID_HANDLE_VALUE) { return entries; }

	do {
		ListEntry entry;
		if (result.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
			entry.Type = PathType::Directory;
		} else {
			entry.Type = PathType::File;
		}

		std::filesystem::path entryPath(result.cFileName);
		if (entryPath == "." || entryPath == "..") { continue; }

		entry.Path = (base / entryPath).string();
		entries.push_back(std::move(entry));
	} while (FindNextFileW(handle, &result));

	FindClose(handle);
	return entries;
}

bool OSFilesystem::MoveReplace(const Path& dst, const Path& src) {
	auto dstP = GetFilesystemPath(dst);
	auto srcP = GetFilesystemPath(src);

	if (MoveFileW(srcP.wstring().c_str(), dstP.wstring().c_str())) { return true; }
	if (GetLastError() != ERROR_ALREADY_EXISTS) { return false; }

	return bool(ReplaceFileW(dstP.wstring().c_str(), srcP.wstring().c_str(), nullptr, 0, nullptr, nullptr));
}

bool OSFilesystem::MoveYield(const Path& dst, const Path& src) {
	auto dstP = GetFilesystemPath(dst);
	auto srcP = GetFilesystemPath(src);

	return bool(MoveFileW(srcP.wstring().c_str(), dstP.wstring().c_str()));
}

FileHandle OSFilesystem::Open(const Path& path, FileMode mode) {
	return OSMappedFile::Open(GetFilesystemPath(path), mode);
}

bool OSFilesystem::Remove(const Path& path) {
	const auto p = GetFilesystemPath(path);

	return bool(DeleteFileW(p.wstring().c_str()));
}

bool OSFilesystem::Stat(const Path& path, FileStat& stat) const {
	const auto p = GetFilesystemPath(path);
	struct __stat64 buffer;
	if (_wstat64(p.wstring().c_str(), &buffer) < 0) { return false; }

	if (buffer.st_mode & _S_IFREG) {
		stat.Type = PathType::File;
	} else if (buffer.st_mode & _S_IFDIR) {
		stat.Type = PathType::Directory;
	} else {
		stat.Type = PathType::Special;
	}

	stat.Size         = uint64_t(buffer.st_size);
	stat.LastModified = buffer.st_mtime;

	return true;
}

void OSFilesystem::UnwatchFile(FileNotifyHandle handle) {
	const auto it = _handlers.find(handle);
	if (it != _handlers.end()) {
		CancelIo(it->second.Handle);
		CloseHandle(it->second.Handle);
		CloseHandle(it->second.Event);
		_handlers.erase(it);
	}
}

void OSFilesystem::Update() {}

FileNotifyHandle OSFilesystem::WatchFile(const Path& path, std::function<void(const FileNotifyInfo&)> func) {
	return -1;
}
}  // namespace Luna
