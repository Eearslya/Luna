#include <Luna/Platform/Filesystem.hpp>
#include <Luna/Project/Project.hpp>
#include <Luna/Project/ProjectSerializer.hpp>
#include <Luna/Utility/Path.hpp>
#include <nlohmann/json.hpp>

using nlohmann::json;

namespace Luna {
ProjectSerializer::ProjectSerializer(Project& project) : _project(project) {}

bool ProjectSerializer::Deserialize(const Path& filePath) {
	auto filesystem = Filesystem::Get();
	FileStat stat;
	if (!filesystem->Stat(filePath, stat) || stat.Size == 0) { return false; }

	std::string projectJson;
	if (!filesystem->ReadFileToString(filePath, projectJson)) { return false; }

	try {
		const auto projectData = json::parse(projectJson);
	} catch (const json::parse_error& e) { return false; }

	return true;
}

bool ProjectSerializer::Serialize(const Path& filePath) {
	auto filesystem = Filesystem::Get();

	json projectData;
	projectData["Name"] = _project.GetSettings().Name;

	const std::string projectJson = projectData.dump();

	return filesystem->WriteStringToFile(filePath, projectJson);
}
}  // namespace Luna
