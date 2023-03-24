#pragma once

#include <Luna/Utility/IntrusivePtr.hpp>
#include <Luna/Utility/Path.hpp>
#include <filesystem>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

namespace Luna {
class FileMapping;

enum class FileMode { ReadOnly, WriteOnly, ReadWrite, WriteOnlyTransactional };
enum class FileNotifyType { FileChanged, FileDeleted, FileCreated };
enum class PathType { File, Directory, Special };

using FileNotifyHandle = int;

struct FileNotifyInfo {
	Path Path;
	FileNotifyType Type;
	FileNotifyHandle Handle;
};

struct FileStat {
	uint64_t Size;
	PathType Type;
	uint64_t LastModified;
};

struct ListEntry {
	Path Path;
	PathType Type;
};

class File : public ThreadSafeIntrusivePtr<File> {
 public:
	virtual ~File() = default;

	IntrusivePtr<FileMapping> Map();

	virtual IntrusivePtr<FileMapping> MapSubset(uint64_t offset, size_t range) = 0;
	virtual IntrusivePtr<FileMapping> MapWrite(size_t range)                   = 0;
	virtual uint64_t GetSize()                                                 = 0;
	virtual void Unmap(void* mapped, size_t range)                             = 0;
};
using FileHandle = IntrusivePtr<File>;

class FileMapping : public ThreadSafeIntrusivePtr<FileMapping> {
 public:
	FileMapping(
		FileHandle handle, uint64_t fileOffset, void* mapped, size_t mappedSize, size_t mapOffset, size_t accessibleSize);
	~FileMapping() noexcept;

	template <typename T = void>
	inline const T* Data() const {
		void* ptr = static_cast<uint8_t*>(_mapped) + _mapOffset;
		return static_cast<const T*>(ptr);
	}

	template <typename T = void>
	inline T* MutableData() {
		void* ptr = static_cast<uint8_t*>(_mapped) + _mapOffset;
		return static_cast<T*>(ptr);
	}

	uint64_t GetFileOffset() const;
	uint64_t GetSize() const;

 private:
	FileHandle _file;
	uint64_t _fileOffset   = 0;
	void* _mapped          = nullptr;
	size_t _mappedSize     = 0;
	size_t _mapOffset      = 0;
	size_t _accessibleSize = 0;
};
using FileMappingHandle = IntrusivePtr<FileMapping>;

class FilesystemBackend {
 public:
	virtual ~FilesystemBackend() = default;

	virtual std::filesystem::path GetFilesystemPath(const Path& path);
	virtual bool MoveReplace(const Path& dst, const Path& src);
	virtual bool MoveYield(const Path& dst, const Path& src);
	void SetProtocol(const std::string& proto);
	std::vector<ListEntry> Walk(const Path& path);
	virtual bool Remove(const Path& path);

	virtual int GetWatchFD() const                                                                        = 0;
	virtual std::vector<ListEntry> List(const Path& path)                                                 = 0;
	virtual FileHandle Open(const Path& path, FileMode mode = FileMode::ReadOnly)                         = 0;
	virtual bool Stat(const Path& path, FileStat& stat)                                                   = 0;
	virtual void UnwatchFile(FileNotifyHandle handle)                                                     = 0;
	virtual void Update()                                                                                 = 0;
	virtual FileNotifyHandle WatchFile(const Path& path, std::function<void(const FileNotifyInfo&)> func) = 0;

 protected:
	std::string _protocol;
};

class Filesystem {
 public:
	Filesystem();

	FilesystemBackend* GetBackend(const std::string& proto = "file");
	void RegisterProtocol(const std::string& proto, std::unique_ptr<FilesystemBackend>&& backend);

	std::filesystem::path GetFilesystemPath(const Path& path);
	std::vector<ListEntry> List(const Path& path);
	bool MoveReplace(const Path& dst, const Path& src);
	bool MoveYield(const Path& dst, const Path& src);
	FileHandle Open(const Path& path, FileMode mode = FileMode::ReadOnly);
	FileMappingHandle OpenReadOnlyMapping(const Path& path);
	FileMappingHandle OpenTransactionalMapping(const Path& path);
	FileMappingHandle OpenWriteOnlyMapping(const Path& path);
	bool ReadFileToString(const Path& path, std::string& outStr);
	bool Remove(const Path& path);
	bool Stat(const Path& path, FileStat& outStat);
	void Update();
	std::vector<ListEntry> Walk(const Path& path);
	bool WriteDataToFile(const Path& path, size_t size, const void* data);
	bool WriteStringToFile(const Path& path, const std::string& str);

	static Filesystem* Get() {
		return _instance;
	}

 private:
	static Filesystem* _instance;

	std::unordered_map<std::string, std::unique_ptr<FilesystemBackend>> _protocols;
};

class ScratchFilesystem : public FilesystemBackend {
 public:
	virtual int GetWatchFD() const override;
	virtual std::vector<ListEntry> List(const Path& path) override;
	virtual FileHandle Open(const Path& path, FileMode mode = FileMode::ReadOnly) override;
	virtual bool Stat(const Path& path, FileStat& stat) override;
	virtual void UnwatchFile(FileNotifyHandle handle) override;
	virtual void Update() override;
	virtual FileNotifyHandle WatchFile(const Path& path, std::function<void(const FileNotifyInfo&)> func) override;

 private:
	using ScratchFile = std::vector<uint8_t>;

	std::unordered_map<Path, std::unique_ptr<ScratchFile>> _files;
};
}  // namespace Luna
