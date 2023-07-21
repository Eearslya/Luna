#include <Luna/Core/Filesystem.hpp>
#include <Luna/Core/OSFilesystem.hpp>

namespace Luna {
static struct FilesystemState { std::unordered_map<std::string, std::unique_ptr<FilesystemBackend>> Protocols; } State;

/* ================
** ===== File =====
*  ================ */

IntrusivePtr<FileMapping> File::Map() {
	return MapSubset(0, GetSize());
}

/* =======================
** ===== FileMapping =====
*  ======================= */

FileMapping::FileMapping(
	FileHandle handle, uint64_t fileOffset, void* mapped, size_t mappedSize, size_t mapOffset, size_t accessibleSize)
		: _file(handle),
			_fileOffset(fileOffset),
			_mapped(mapped),
			_mappedSize(mappedSize),
			_mapOffset(mapOffset),
			_accessibleSize(accessibleSize) {}

FileMapping::~FileMapping() noexcept {
	if (_file) { _file->Unmap(_mapped, _mappedSize); }
}

uint64_t FileMapping::GetFileOffset() const {
	return _fileOffset;
}

uint64_t FileMapping::GetSize() const {
	return _accessibleSize;
}

/* =============================
** ===== FilesystemBackend =====
*  ============================= */

std::filesystem::path FilesystemBackend::GetFilesystemPath(const Path& path) const {
	return "";
}

bool FilesystemBackend::MoveReplace(const Path& dst, const Path& src) {
	return false;
}

bool FilesystemBackend::MoveYield(const Path& dst, const Path& src) {
	return false;
}

void FilesystemBackend::SetProtocol(std::string_view proto) {
	_protocol = std::string(proto);
}

std::vector<ListEntry> FilesystemBackend::Walk(const Path& path) {
	auto entries = List(path);
	std::vector<ListEntry> finalEntries;
	for (auto& e : entries) {
		if (e.Type == PathType::Directory) {
			auto subentries = Walk(e.Path);
			finalEntries.push_back(std::move(e));
			for (auto& sub : subentries) { finalEntries.push_back(std::move(sub)); }
		} else if (e.Type == PathType::File) {
			finalEntries.push_back(std::move(e));
		}
	}

	return finalEntries;
}

bool FilesystemBackend::Remove(const Path& path) {
	return false;
}

/* ======================
** ===== Filesystem =====
*  ====================== */

bool Filesystem::Initialize() {
	RegisterProtocol("file", std::unique_ptr<FilesystemBackend>(new OSFilesystem(".")));
	RegisterProtocol("memory", std::unique_ptr<FilesystemBackend>(new ScratchFilesystem));

	return true;
}

void Filesystem::Shutdown() {
	State.Protocols.clear();
}

FilesystemBackend* Filesystem::GetBackend(std::string_view proto) {
	const auto it = State.Protocols.find(proto.empty() ? "file" : std::string(proto));
	if (it == State.Protocols.end()) { return nullptr; }

	return it->second.get();
}

void Filesystem::RegisterProtocol(std::string_view proto, std::unique_ptr<FilesystemBackend>&& backend) {
	backend->SetProtocol(proto);
	State.Protocols[std::string(proto)] = std::move(backend);
}

void Filesystem::UnregisterProtocol(std::string_view proto) {
	const auto it = State.Protocols.find(std::string(proto));
	if (it != State.Protocols.end()) { State.Protocols.erase(it); }
}

bool Filesystem::Exists(const Path& path) {
	FileStat stat;
	return Stat(path, stat);
}

std::filesystem::path Filesystem::GetFilesystemPath(const Path& path) {
	auto* backend = GetBackend(path.Protocol());
	if (!backend) { return {}; }

	return backend->GetFilesystemPath(path.FilePath());
}

std::vector<ListEntry> Filesystem::List(const Path& path) {
	auto* backend = GetBackend(path.Protocol());
	if (!backend) { return {}; }

	return backend->List(path.FilePath());
}

bool Filesystem::MoveReplace(const Path& dst, const Path& src) {
	auto* dstBackend = GetBackend(dst.Protocol());
	auto* srcBackend = GetBackend(src.Protocol());
	if (!dstBackend || !srcBackend || dstBackend != srcBackend) { return false; }

	return dstBackend->MoveReplace(dst.FilePath(), src.FilePath());
}

bool Filesystem::MoveYield(const Path& dst, const Path& src) {
	auto* dstBackend = GetBackend(dst.Protocol());
	auto* srcBackend = GetBackend(src.Protocol());
	if (!dstBackend || !srcBackend || dstBackend != srcBackend) { return false; }

	return dstBackend->MoveYield(dst.FilePath(), src.FilePath());
}

FileHandle Filesystem::Open(const Path& path, FileMode mode) {
	auto* backend = GetBackend(path.Protocol());
	if (!backend) { return {}; }

	return backend->Open(path.FilePath(), mode);
}

FileMappingHandle Filesystem::OpenReadOnlyMapping(const Path& path) {
	auto file = Open(path, FileMode::ReadOnly);
	if (!file) { return {}; }

	return file->Map();
}

FileMappingHandle Filesystem::OpenTransactionalMapping(const Path& path, size_t size) {
	auto file = Open(path, FileMode::WriteOnlyTransactional);
	if (!file) { return {}; }

	return file->Map();
}

FileMappingHandle Filesystem::OpenWriteOnlyMapping(const Path& path) {
	auto file = Open(path, FileMode::WriteOnly);
	if (!file) { return {}; }

	return file->Map();
}

bool Filesystem::ReadFileToString(const Path& path, std::string& outStr) {
	auto mapping = OpenReadOnlyMapping(path);
	if (!mapping) { return false; }

	const auto size = mapping->GetSize();
	outStr          = std::string(mapping->Data<char>(), mapping->Data<char>() + size);
	outStr.erase(std::remove_if(outStr.begin(), outStr.end(), [](char c) { return c == '\r'; }), outStr.end());

	return true;
}

bool Filesystem::Remove(const Path& path) {
	auto* backend = GetBackend(path.Protocol());
	if (!backend) { return {}; }

	return backend->Remove(path.FilePath());
}

bool Filesystem::Stat(const Path& path, FileStat& outStat) {
	auto* backend = GetBackend(path.Protocol());
	if (!backend) { return {}; }

	return backend->Stat(path.FilePath(), outStat);
}

void Filesystem::Update() {
	for (auto& proto : State.Protocols) { proto.second->Update(); }
}

std::vector<ListEntry> Filesystem::Walk(const Path& path) {
	auto* backend = GetBackend(path.Protocol());
	if (!backend) { return {}; }

	return backend->Walk(path.FilePath());
}

bool Filesystem::WriteDataToFile(const Path& path, size_t size, const void* data) {
	auto file = OpenTransactionalMapping(path, size);
	if (!file) { return false; }

	memcpy(file->MutableData(), data, size);

	return true;
}

bool Filesystem::WriteStringToFile(const Path& path, std::string_view str) {
	return WriteDataToFile(path, str.size(), str.data());
}

/* =============================
** ===== ScratchFilesystem =====
*  ============================= */

class ScratchFilesystemFile : public File {
 public:
	ScratchFilesystemFile(std::vector<uint8_t>& data) : _data(data) {}

	virtual IntrusivePtr<FileMapping> MapSubset(uint64_t offset, size_t range) override {
		if (offset + range > _data.size()) { return {}; }

		return MakeHandle<FileMapping>(FileHandle{}, offset, _data.data() + offset, range, 0, range);
	}

	virtual IntrusivePtr<FileMapping> MapWrite(size_t range) override {
		_data.resize(range);

		return MapSubset(0, range);
	}

	virtual uint64_t GetSize() override {
		return _data.size();
	}

	virtual void Unmap(void* mapped, size_t range) override {}

 private:
	std::vector<uint8_t>& _data;
};

int ScratchFilesystem::GetWatchFD() const {
	return -1;
}

std::vector<ListEntry> ScratchFilesystem::List(const Path& path) {
	if (!path.IsRoot()) { return {}; }

	std::vector<ListEntry> list;
	for (const auto& [path, file] : _files) { list.push_back({path, PathType::File}); }

	return list;
}

FileHandle ScratchFilesystem::Open(const Path& path, FileMode mode) {
	const auto it = _files.find(path);
	if (it == _files.end()) {
		auto& file = _files[path];
		file       = std::make_unique<ScratchFile>();

		return MakeHandle<ScratchFilesystemFile>(*file);
	} else {
		return MakeHandle<ScratchFilesystemFile>(*it->second);
	}
}

bool ScratchFilesystem::Stat(const Path& path, FileStat& stat) const {
	const auto it = _files.find(path);
	if (it == _files.end()) { return false; }

	stat.Size         = it->second->size();
	stat.Type         = PathType::File;
	stat.LastModified = 0;

	return true;
}

void ScratchFilesystem::UnwatchFile(FileNotifyHandle handle) {}

void ScratchFilesystem::Update() {}

FileNotifyHandle ScratchFilesystem::WatchFile(const Path& path, std::function<void(const FileNotifyInfo&)> func) {
	return -1;
}
}  // namespace Luna
