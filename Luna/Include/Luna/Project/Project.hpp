#pragma once

#include <Luna/Utility/IntrusivePtr.hpp>
#include <Luna/Utility/Path.hpp>

namespace Luna {
class Project;
using ProjectHandle = IntrusivePtr<Project>;

struct ProjectSettings {
	std::string Name = "Project";
};

class Project : public IntrusivePtrEnabled<Project> {
 public:
	Project(const Path& projectPath);
	~Project() noexcept;

	const Path& GetFile() const {
		return _projectFile;
	}
	const Path& GetPath() const {
		return _projectPath;
	}
	ProjectSettings& GetSettings() {
		return _projectSettings;
	}
	const ProjectSettings& GetSettings() const {
		return _projectSettings;
	}

	bool Load();
	bool Save();

	static ProjectHandle GetActive() {
		return _activeProject;
	}
	static void SetActive(ProjectHandle active);

	static Path AssetPath();
	static Path AssetRegistry();
	static Path& ProjectFile();
	static Path& ProjectPath();

 private:
	static ProjectHandle _activeProject;

	Path _projectPath;
	Path _projectFile;
	ProjectSettings _projectSettings;
};
}  // namespace Luna
