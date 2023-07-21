#include <Luna/Core/OSFilesystem.hpp>
#include <Luna/Utility/Timer.hpp>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

namespace Luna {
struct WatchHandler {
	void Update() {
		Overlapped        = {};
		Overlapped.hEvent = Event;

		auto ret = ::ReadDirectoryChangesW(
			Handle,
			AsyncBuffer,
			sizeof(AsyncBuffer),
			FALSE,
			FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_CREATION | FILE_NOTIFY_CHANGE_FILE_NAME,
			nullptr,
			&Overlapped,
			nullptr);
		if (!ret && GetLastError() != ERROR_IO_PENDING) { Log::Error("Filesystem", "Failed to read directory changes"); }

		SinceLastEvent.Start();
	}

	Path Path;
	std::function<void(const FileNotifyInfo&)> Function;
	HANDLE Handle = nullptr;
	HANDLE Event  = nullptr;
	DWORD AsyncBuffer[1024];
	OVERLAPPED Overlapped;
	Timer SinceLastEvent;
};

struct WindowsState {
	FileNotifyHandle NextHandle = 0;
	std::unordered_map<FileNotifyHandle, WatchHandler> Handlers;
};

struct PageSizeQuery {
	PageSizeQuery() {
		SYSTEM_INFO systemInfo = {};
		::GetSystemInfo(&systemInfo);
		PageSize = systemInfo.dwPageSize;
	}

	uint32_t PageSize = 0;
};

class OSMappedFile : public File {
 public:
	OSMappedFile(const std::filesystem::path& path, FileMode mode) {
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

	~OSMappedFile() noexcept {
		if (_fileMapping) { ::CloseHandle(_fileMapping); }
		if (_file != INVALID_HANDLE_VALUE) { ::CloseHandle(_file); }
	}

	virtual IntrusivePtr<FileMapping> MapSubset(uint64_t offset, size_t range) override {
		static const PageSizeQuery pageSizeQuery;

		if (offset + range > _size) { return {}; }
		if (!_fileMapping) { return {}; }

		const uint64_t beginMap   = offset & ~uint64_t(pageSizeQuery.PageSize - 1);
		const DWORD hi            = DWORD(beginMap >> 32);
		const DWORD lo            = DWORD(beginMap & 0xffffffff);
		const uint64_t endMapping = offset + range;
		const size_t mappedSize   = endMapping - beginMap;

		void* mapped = ::MapViewOfFile(_fileMapping, FILE_MAP_READ, hi, lo, mappedSize);
		if (!mapped) { return {}; }

		return MakeHandle<FileMapping>(
			ReferenceFromThis(), offset, static_cast<uint8_t*>(mapped) + beginMap, mappedSize, offset - beginMap, range);
	}

	virtual IntrusivePtr<FileMapping> MapWrite(size_t range) override {
		_size          = range;
		const DWORD hi = DWORD(range >> 32);
		const DWORD lo = DWORD(range & 0xffffffff);

		HANDLE fileView = ::CreateFileMappingW(_file, nullptr, PAGE_READWRITE, hi, lo, nullptr);
		if (!fileView) { return {}; }

		void* mapped = ::MapViewOfFile(fileView, FILE_MAP_ALL_ACCESS, 0, 0, range);
		::CloseHandle(fileView);
		if (!mapped) { return {}; }

		return MakeHandle<FileMapping>(ReferenceFromThis(), 0, static_cast<uint8_t*>(mapped), range, 0, range);
	}

	virtual uint64_t GetSize() override {
		return _size;
	}

	virtual void Unmap(void* mapped, size_t range) override {
		if (mapped) { ::UnmapViewOfFile(mapped); }
	}

	static FileHandle Open(const std::filesystem::path& path, FileMode mode) {
		try {
			return MakeHandle<OSMappedFile>(path, mode);
		} catch (const std::exception& e) {
			Log::Error("Filesystem",
			           "Failed to open file '{}' for {}: {}",
			           path.string(),
			           mode == FileMode::ReadOnly ? "reading" : "writing",
			           e.what());

			return {};
		}
	}

 private:
	HANDLE _file        = INVALID_HANDLE_VALUE;
	HANDLE _fileMapping = nullptr;
	uint64_t _size      = 0;
	std::filesystem::path _renameFromOnClose;
	std::filesystem::path _renameToOnClose;
};

OSFilesystem::OSFilesystem(const Path& base) : _basePath(base.String()) {
	std::filesystem::create_directories(GetFilesystemPath(""));
	_data.reset(reinterpret_cast<uint8_t*>(new WindowsState));
}

OSFilesystem::~OSFilesystem() noexcept {
	if (_data) {
		WindowsState* data = reinterpret_cast<WindowsState*>(_data.get());
		for (auto& handler : data->Handlers) {
			::CancelIo(handler.second.Handle);
			::CloseHandle(handler.second.Handle);
			::CloseHandle(handler.second.Event);
		}
	}
}

std::filesystem::path OSFilesystem::GetFilesystemPath(const Path& path) const {
	const auto norm = path.Normalized();
	if (!norm.ValidateBounds()) { return ""; }

	if (path.IsAbsolute()) { return _basePath / std::string(norm).substr(1); }

	return _basePath / std::string(norm);
}

bool OSFilesystem::MoveReplace(const Path& dst, const Path& src) {
	if (!dst.ValidateBounds() || !src.ValidateBounds()) { return false; }

	auto dstPath = GetFilesystemPath(dst.Normalized());
	auto srcPath = GetFilesystemPath(src.Normalized());

	if (::MoveFileW(srcPath.wstring().c_str(), dstPath.wstring().c_str())) { return true; }
	if (::GetLastError() != ERROR_ALREADY_EXISTS) { return false; }

	return bool(::ReplaceFileW(dstPath.wstring().c_str(), srcPath.wstring().c_str(), nullptr, 0, nullptr, nullptr));
}

bool OSFilesystem::MoveYield(const Path& dst, const Path& src) {
	if (!dst.ValidateBounds() || !src.ValidateBounds()) { return false; }

	auto dstPath = GetFilesystemPath(dst.Normalized());
	auto srcPath = GetFilesystemPath(src.Normalized());

	return bool(::MoveFileW(srcPath.wstring().c_str(), dstPath.wstring().c_str()));
}

bool OSFilesystem::Remove(const Path& path) {
	if (!path.ValidateBounds()) { return false; }

	const auto p = GetFilesystemPath(path.Normalized());

	return bool(::DeleteFileW(p.wstring().c_str()));
}

int OSFilesystem::GetWatchFD() const {
	return -1;
}

std::vector<ListEntry> OSFilesystem::List(const Path& path) {
	if (!path.ValidateBounds()) { return {}; }

	const std::filesystem::path base = std::string(path);
	std::vector<ListEntry> entries;
	WIN32_FIND_DATAW result;
	auto p = GetFilesystemPath(path);
	p /= L"*";

	HANDLE handle = ::FindFirstFileW(p.wstring().c_str(), &result);
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
	} while (::FindNextFileW(handle, &result));

	::FindClose(handle);

	return entries;
}

FileHandle OSFilesystem::Open(const Path& path, FileMode mode) {
	if (!path.ValidateBounds()) { return {}; }

	return OSMappedFile::Open(GetFilesystemPath(path), mode);
}

bool OSFilesystem::Stat(const Path& path, FileStat& stat) const {
	if (!path.ValidateBounds()) { return false; }

	const auto p = GetFilesystemPath(path);
	struct __stat64 buffer;
	if (::_wstat64(p.wstring().c_str(), &buffer) < 0) { return false; }

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
	if (!_data) { return; }
	WindowsState* data = reinterpret_cast<WindowsState*>(_data.get());

	const auto it = data->Handlers.find(handle);
	if (it != data->Handlers.end()) {
		::CancelIo(it->second.Handle);
		::CloseHandle(it->second.Handle);
		::CloseHandle(it->second.Event);
		data->Handlers.erase(it);
	}
}

void OSFilesystem::Update() {
	if (!_data) { return; }
	WindowsState* data = reinterpret_cast<WindowsState*>(_data.get());

	for (auto& handler : data->Handlers) {
		if (::WaitForSingleObject(handler.second.Event, 0) != WAIT_OBJECT_0) { continue; }

		// Windows is sending two events for every file write. To prevent executing the callback twice, we ensure that at
		// least one second has passed since our last change event.
		if (handler.second.SinceLastEvent.End() < 1.0) {
			handler.second.Update();
			continue;
		}

		DWORD bytesReturned;
		if (!::GetOverlappedResult(handler.second.Handle, &handler.second.Overlapped, &bytesReturned, TRUE)) { continue; }

		size_t offset                       = 0;
		const FILE_NOTIFY_INFORMATION* info = nullptr;
		do {
			info = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(
				reinterpret_cast<const uint8_t*>(handler.second.AsyncBuffer) + offset);

			const std::wstring filePathStr(&info->FileName[0], info->FileNameLength / sizeof(WCHAR));
			const std::filesystem::path filePath(filePathStr);

			FileNotifyInfo notify;
			notify.Handle = handler.first;
			notify.Path   = handler.second.Path / filePath.string();

			switch (info->Action) {
				case FILE_ACTION_ADDED:
				case FILE_ACTION_RENAMED_NEW_NAME:
					notify.Type = FileNotifyType::FileCreated;
					if (handler.second.Function) { handler.second.Function(notify); }
					break;

				case FILE_ACTION_REMOVED:
				case FILE_ACTION_RENAMED_OLD_NAME:
					notify.Type = FileNotifyType::FileDeleted;
					if (handler.second.Function) { handler.second.Function(notify); }
					break;

				case FILE_ACTION_MODIFIED:
					notify.Type = FileNotifyType::FileChanged;
					if (handler.second.Function) { handler.second.Function(notify); }
					break;

				default:
					break;
			}
		} while (info->NextEntryOffset != 0);

		handler.second.Update();
	}
}

FileNotifyHandle OSFilesystem::WatchFile(const Path& path, std::function<void(const FileNotifyInfo&)> func) {
	if (!_data) { return -1; }
	WindowsState* data = reinterpret_cast<WindowsState*>(_data.get());
	if (!path.ValidateBounds()) { return -1; }

	FileStat stat = {};
	if (!Stat(path, stat)) {
		Log::Error("Filesystem", "Cannot watch path '{}': File or folder does not exist.", path);

		return -1;
	}

	if (stat.Type != PathType::Directory) {
		Log::Error("Filesystem", "Cannot watch path '{}': Windows filesystem only supports directory watching.", path);

		return -1;
	}

	const auto fsPath = GetFilesystemPath(path);
	HANDLE handle     = ::CreateFileW(fsPath.c_str(),
                                FILE_LIST_DIRECTORY,
                                FILE_SHARE_WRITE | FILE_SHARE_READ | FILE_SHARE_DELETE,
                                nullptr,
                                OPEN_EXISTING,
                                FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
                                nullptr);
	if (handle == INVALID_HANDLE_VALUE) {
		Log::Error("Filesystem", "Cannot watch path '{}': Failed to open directory.", path);

		return -1;
	}

	HANDLE event = ::CreateEvent(nullptr, FALSE, FALSE, nullptr);
	if (event == nullptr) {
		::CloseHandle(handle);

		return -1;
	}

	data->NextHandle++;

	WatchHandler handler;
	handler.Path     = _protocol + "://" + path.String();
	handler.Function = std::move(func);
	handler.Handle   = handle;
	handler.Event    = event;

	auto& h = data->Handlers[data->NextHandle];
	h       = std::move(handler);
	h.Update();

	return data->NextHandle;
}
}  // namespace Luna
