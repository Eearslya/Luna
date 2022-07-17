#pragma once

#include <filesystem>

#include "Utility/IntrusivePtr.hpp"

namespace Luna {
struct ProjectSettings {
	std::string Name;

	std::filesystem::path AssetDirectory;
	std::filesystem::path AssetRegistryPath;

	std::filesystem::path ProjectFileName;
	std::filesystem::path ProjectDirectory;
};

class Project;
using ProjectHandle = IntrusivePtr<Project>;

class Project : public IntrusivePtrEnabled<Project> {
 public:
	Project()           = default;
	~Project() noexcept = default;

	const ProjectSettings& GetSettings() const {
		return _settings;
	}

	static ProjectHandle GetActive() {
		return _activeProject;
	}
	static void SetActive(ProjectHandle project);

 private:
	static ProjectHandle _activeProject;

	ProjectSettings _settings;
};
}  // namespace Luna
