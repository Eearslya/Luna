#pragma once

#include <Luna/Platform/Filesystem.hpp>

namespace Luna {
class ProjectFilesystem : public FilesystemBackend {
 public:
	virtual int GetWatchFD() const override;
	virtual std::vector<ListEntry> List(const Path& path) override;
	virtual FileHandle Open(const Path& path, FileMode mode = FileMode::ReadOnly) override;
	virtual bool Stat(const Path& path, FileStat& stat) const override;
	virtual void UnwatchFile(FileNotifyHandle handle) override;
	virtual void Update() override;
	virtual FileNotifyHandle WatchFile(const Path& path, std::function<void(const FileNotifyInfo&)> func) override;
};
}  // namespace Luna
