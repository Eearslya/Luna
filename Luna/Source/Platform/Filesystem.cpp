#include <Luna/Platform/Filesystem.hpp>
#include <Luna/Platform/Windows/OSFilesystem.hpp>
#include <Tracy/Tracy.hpp>

namespace Luna {
static struct FilesystemState { std::unordered_map<std::string, std::unique_ptr<FilesystemBackend>> Protocols; } State;

// ================
// ===== File =====
// ================
IntrusivePtr<FileMapping> File::Map() {
	return MapSubset(0, GetSize());
}

// =======================
// ===== FileMapping =====
// =======================
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

// =============================
// ===== FilesystemBackend =====
// =============================
std::filesystem::path FilesystemBackend::GetFilesystemPath(const Path& path) const {
	return "";
}

bool FilesystemBackend::MoveReplace(const Path& dst, const Path& src) {
	return false;
}

bool FilesystemBackend::MoveYield(const Path& dst, const Path& src) {
	return false;
}

bool FilesystemBackend::Remove(const Path& path) {
	return false;
}

void FilesystemBackend::SetProtocol(const std::string& proto) {
	_protocol = proto;
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

// ======================
// ===== Filesystem =====
// ======================
bool Filesystem::Initialize() {
	ZoneScopedN("Filesystem::Initialize");

	RegisterProtocol("file", std::unique_ptr<FilesystemBackend>(new OSFilesystem(".")));
	RegisterProtocol("memory", std::unique_ptr<FilesystemBackend>(new ScratchFilesystem));

	return true;
}

void Filesystem::Shutdown() {
	ZoneScopedN("Filesystem::Shutdown");

	State.Protocols.clear();
}

FilesystemBackend* Filesystem::GetBackend(const std::string& proto) {
	const auto it = State.Protocols.find(proto.empty() ? "file" : proto);
	if (it == State.Protocols.end()) { return nullptr; }

	return it->second.get();
}

void Filesystem::RegisterProtocol(const std::string& proto, std::unique_ptr<FilesystemBackend>&& backend) {
	backend->SetProtocol(proto);
	State.Protocols[proto] = std::move(backend);
}

bool Filesystem::Exists(const Path& path) {
	FileStat stat;
	return Stat(path, stat);
}

std::filesystem::path Filesystem::GetFilesystemPath(const Path& path) {
	const auto parts = path.ProtocolSplit();
	auto* backend    = GetBackend(parts.first);
	if (!backend) { return {}; }

	return backend->GetFilesystemPath(parts.second);
}

std::vector<ListEntry> Filesystem::List(const Path& path) {
	const auto parts = path.ProtocolSplit();
	auto* backend    = GetBackend(parts.first);
	if (!backend) { return {}; }

	return backend->List(parts.second);
}

bool Filesystem::MoveReplace(const Path& dst, const Path& src) {
	const auto dstParts = dst.ProtocolSplit();
	const auto srcParts = src.ProtocolSplit();
	auto* backendDst    = GetBackend(dstParts.first);
	auto* backendSrc    = GetBackend(srcParts.first);
	if (!backendDst || !backendSrc || backendDst != backendSrc) { return false; }

	return backendDst->MoveReplace(dstParts.second, srcParts.second);
}

bool Filesystem::MoveYield(const Path& dst, const Path& src) {
	const auto dstParts = dst.ProtocolSplit();
	const auto srcParts = src.ProtocolSplit();
	auto* backendDst    = GetBackend(dstParts.first);
	auto* backendSrc    = GetBackend(srcParts.first);
	if (!backendDst || !backendSrc || backendDst != backendSrc) { return false; }

	return backendDst->MoveYield(dstParts.second, srcParts.second);
}

FileHandle Filesystem::Open(const Path& path, FileMode mode) {
	const auto parts = path.ProtocolSplit();
	auto* backend    = GetBackend(parts.first);
	if (!backend) { return {}; }

	return backend->Open(parts.second, mode);
}

FileMappingHandle Filesystem::OpenReadOnlyMapping(const Path& path) {
	auto file = Open(path, FileMode::ReadOnly);
	if (!file) { return {}; }

	return file->Map();
}

FileMappingHandle Filesystem::OpenTransactionalMapping(const Path& path, size_t size) {
	auto file = Open(path, FileMode::WriteOnlyTransactional);
	if (!file) { return {}; }

	return file->MapWrite(size);
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
	const auto parts = path.ProtocolSplit();
	auto* backend    = GetBackend(parts.first);
	if (!backend) { return {}; }

	return backend->Remove(parts.second);
}

bool Filesystem::Stat(const Path& path, FileStat& outStat) {
	const auto parts = path.ProtocolSplit();
	auto* backend    = GetBackend(parts.first);
	if (!backend) { return false; }

	return backend->Stat(parts.second, outStat);
}

void Filesystem::Update() {
	ZoneScopedN("Filesystem::Update");

	for (auto& proto : State.Protocols) { proto.second->Update(); }
}

std::vector<ListEntry> Filesystem::Walk(const Path& path) {
	const auto parts = path.ProtocolSplit();
	auto* backend    = GetBackend(parts.first);
	if (!backend) { return {}; }

	return backend->Walk(parts.second);
}

bool Filesystem::WriteDataToFile(const Path& path, size_t size, const void* data) {
	auto file = OpenTransactionalMapping(path, size);
	if (!file) { return false; }

	memcpy(file->MutableData(), data, size);

	return true;
}

bool Filesystem::WriteStringToFile(const Path& path, const std::string& str) {
	return WriteDataToFile(path, str.size(), str.data());
}

// =============================
// ===== ScratchFilesystem =====
// =============================
struct ScratchFilesystemFile : public File {
	ScratchFilesystemFile(std::vector<uint8_t>& data) : Data(data) {}

	virtual uint64_t GetSize() override {
		return Data.size();
	}
	virtual FileMappingHandle MapSubset(uint64_t offset, size_t range) override {
		if (offset + range > Data.size()) { return {}; }

		return MakeHandle<FileMapping>(FileHandle{}, offset, Data.data() + offset, range, 0, range);
	}
	virtual FileMappingHandle MapWrite(size_t size) override {
		Data.resize(size);

		return MapSubset(0, size);
	}
	virtual void Unmap(void*, size_t) override {}

	std::vector<uint8_t>& Data;
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
