#pragma once

#include <Luna/Platform/Filesystem.hpp>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

namespace Luna {
class OSMappedFile : public File {
 public:
	OSMappedFile(const std::filesystem::path& path, FileMode mode);
	~OSMappedFile() noexcept;

	virtual uint64_t GetSize() override;
	virtual FileMappingHandle MapSubset(uint64_t offset, size_t range) override;
	virtual FileMappingHandle MapWrite(size_t size) override;
	virtual void Unmap(void* mapped, size_t range) override;

	static FileHandle Open(const std::filesystem::path& path, FileMode mode);

 private:
	HANDLE _file        = INVALID_HANDLE_VALUE;
	HANDLE _fileMapping = nullptr;
	uint64_t _size      = 0;
	std::filesystem::path _renameFromOnClose;
	std::filesystem::path _renameToOnClose;
};

class OSFilesystem : public FilesystemBackend {
 public:
	OSFilesystem(const Path& base);
	~OSFilesystem() noexcept;

	virtual std::filesystem::path GetFilesystemPath(const Path& path) const override;
	virtual int GetWatchFD() const override;
	virtual std::vector<ListEntry> List(const Path& path) override;
	virtual bool MoveReplace(const Path& dst, const Path& src) override;
	virtual bool MoveYield(const Path& dst, const Path& src) override;
	virtual FileHandle Open(const Path& path, FileMode mode = FileMode::ReadOnly) override;
	virtual bool Remove(const Path& path) override;
	virtual bool Stat(const Path& path, FileStat& stat) const override;
	virtual void UnwatchFile(FileNotifyHandle handle) override;
	virtual void Update() override;
	virtual FileNotifyHandle WatchFile(const Path& path, std::function<void(const FileNotifyInfo&)> func) override;

 private:
	struct Handler {
		Path Path;
		std::function<void(const FileNotifyInfo&)> Function;
		HANDLE Handle = nullptr;
		HANDLE Event  = nullptr;
		DWORD AsyncBuffer[1024];
		OVERLAPPED Overlapped;
	};

	std::filesystem::path _basePath;
	std::unordered_map<FileNotifyHandle, Handler> _handlers;
	FileNotifyHandle _handleId = 0;
};
}  // namespace Luna
