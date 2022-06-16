#include <Luna/Core/Log.hpp>
#include <Luna/Core/Project.hpp>
#include <Luna/Filesystem/Filesystem.hpp>
#include <fstream>
#include <json.hpp>

using nlohmann::json;

namespace Luna {
Project::Project(const std::filesystem::path& projectPath, bool create) {
	_projectPath     = projectPath.parent_path();
	_projectFilePath = projectPath;

	if (!std::filesystem::exists(projectPath)) {
		if (create) {
			Create();
		} else {
			throw std::runtime_error("Failed to open project file!");
		}
	}

	Load();
}

Project::~Project() noexcept {}

void Project::Load() {
	try {
		json jsonData;
		std::ifstream projectFile(_projectFilePath);
		projectFile >> jsonData;
		projectFile.close();

		json project      = jsonData["project"];
		Name              = project["Name"].get<std::string>();
		AssetDirectory    = project["AssetDirectory"].get<std::string>();
		AssetRegistryPath = project["AssetRegistryPath"].get<std::string>();
	} catch (const std::exception& e) {
		Log::Error("Project", "Failed to load Project file '{}': {}", _projectFilePath.string(), e.what());
		throw;
	}

	_assetDirectoryPath = _projectPath / AssetDirectory;
	_assetRegistryPath  = _projectPath / AssetRegistryPath;

	Log::Info("Project", "Loaded project '{}' successfully.", Name);
}

void Project::Save() const {
	try {
		Log::Info("Project", "Saving Project file '{}'.", _projectFilePath.string());

		json jsonData                = json::object();
		json project                 = json::object();
		project["Name"]              = Name;
		project["AssetDirectory"]    = AssetDirectory;
		project["AssetRegistryPath"] = AssetRegistryPath;
		jsonData["project"]          = project;

		std::ofstream projectFile(_projectFilePath);
		projectFile << jsonData.dump(2) << std::endl;
		projectFile.close();
	} catch (const std::exception& e) {
		Log::Error("Project", "Failed to save Project file '{}': {}", _projectFilePath.string(), e.what());
		throw;
	}
}

std::filesystem::path Project::GetProjectsFolder() {
	const auto documentsPath                   = Filesystem::Get()->GetSpecialFolder(SpecialFolder::Documents);
	const std::filesystem::path projectsFolder = documentsPath / "Luna Projects";
	if (!std::filesystem::exists(projectsFolder)) { std::filesystem::create_directories(projectsFolder); }

	return projectsFolder;
}

void Project::Create() {
	Log::Info("Project", "Creating new Project: '{}'", _projectFilePath.string());

	_assetDirectoryPath = _projectPath / AssetDirectory;

	if (!std::filesystem::exists(_projectPath)) { std::filesystem::create_directories(_projectPath); }
	if (!std::filesystem::exists(_assetDirectoryPath)) { std::filesystem::create_directories(_assetDirectoryPath); }

	Save();
}
}  // namespace Luna
