#include <Luna/Assets/AssetManager.hpp>
#include <Luna/Project/Project.hpp>
#include <Luna/Project/ProjectSerializer.hpp>

namespace Luna {
ProjectHandle Project::_activeProject(nullptr);

Project::Project(const Path& projectPath) : _projectPath(projectPath.BaseDirectory()), _projectFile(projectPath) {}

Project::~Project() noexcept {}

bool Project::Load() {
	ProjectSerializer serializer(*this);
	return serializer.Deserialize(_projectFile);
}

bool Project::Save() {
	ProjectSerializer serializer(*this);
	return serializer.Serialize(_projectFile);
}

void Project::SetActive(ProjectHandle active) {
	if (_activeProject) { AssetManager::Shutdown(); }
	_activeProject = active;
	if (_activeProject) { AssetManager::Initialize(); }
}

Path Project::AssetPath() {
	return ProjectPath() / "Assets";
}

Path Project::AssetRegistry() {
	return ProjectPath() / "AssetRegistry.lregistry";
}

Path& Project::ProjectFile() {
	return _activeProject->_projectFile;
}

Path& Project::ProjectPath() {
	return _activeProject->_projectPath;
}
}  // namespace Luna
