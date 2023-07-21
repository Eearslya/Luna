#pragma once

#include <Luna/Core/Filesystem.hpp>

namespace Luna {
class OSFilesystem : public FilesystemBackend {
 public:
	OSFilesystem(const Path& base);
	~OSFilesystem() noexcept;

	virtual std::filesystem::path GetFilesystemPath(const Path& path) const override;
	virtual bool MoveReplace(const Path& dst, const Path& src) override;
	virtual bool MoveYield(const Path& dst, const Path& src) override;
	virtual bool Remove(const Path& path) override;

	virtual int GetWatchFD() const override;
	virtual std::vector<ListEntry> List(const Path& path) override;
	virtual FileHandle Open(const Path& path, FileMode mode = FileMode::ReadOnly) override;
	virtual bool Stat(const Path& path, FileStat& stat) const override;
	virtual void UnwatchFile(FileNotifyHandle handle) override;
	virtual void Update() override;
	virtual FileNotifyHandle WatchFile(const Path& path, std::function<void(const FileNotifyInfo&)> func) override;

 protected:
	std::filesystem::path _basePath;
	std::unique_ptr<uint8_t> _data;
};
}  // namespace Luna
