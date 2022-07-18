#include "Project.hpp"

#include "Assets/AssetManager.hpp"

namespace Luna {
ProjectHandle Project::_activeProject(nullptr);

void Project::SetActive(ProjectHandle project) {
	if (_activeProject) {}
	_activeProject = project;
	if (_activeProject) {}
}
}  // namespace Luna
