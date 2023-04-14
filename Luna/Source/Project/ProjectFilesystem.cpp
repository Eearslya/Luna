#include <Luna/Project/Project.hpp>
#include <Luna/Project/ProjectFilesystem.hpp>

namespace Luna {
static FilesystemBackend* ProjectBackend() {
	return Filesystem::GetBackend(Project::ProjectPath().Protocol());
}

static Path ProjectPath(const Path& path) {
	const auto [proto, p] = Project::ProjectPath().ProtocolSplit();
	return Path(p) / path;
}

int ProjectFilesystem::GetWatchFD() const {
	return ProjectBackend()->GetWatchFD();
}

std::vector<ListEntry> ProjectFilesystem::List(const Path& path) {
	return ProjectBackend()->List(ProjectPath(path));
}

FileHandle ProjectFilesystem::Open(const Path& path, FileMode mode) {
	return ProjectBackend()->Open(ProjectPath(path), mode);
}

bool ProjectFilesystem::Stat(const Path& path, FileStat& stat) const {
	return ProjectBackend()->Stat(ProjectPath(path), stat);
}

void ProjectFilesystem::UnwatchFile(FileNotifyHandle handle) {
	return ProjectBackend()->UnwatchFile(handle);
}

void ProjectFilesystem::Update() {}

FileNotifyHandle ProjectFilesystem::WatchFile(const Path& path, std::function<void(const FileNotifyInfo&)> func) {
	return ProjectBackend()->WatchFile(ProjectPath(path), std::move(func));
}
}  // namespace Luna
