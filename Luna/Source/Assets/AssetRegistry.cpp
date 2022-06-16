#include <Luna/Assets/AssetRegistry.hpp>
#include <Luna/Core/Engine.hpp>
#include <Luna/Core/Log.hpp>
#include <Luna/Core/Project.hpp>
#include <fstream>
#include <json.hpp>

using nlohmann::json;

namespace Luna {
std::filesystem::path AssetRegistry::GetFilesystemPath(const AssetMetadata& metadata) const {
	return Engine::Get()->GetProject()->GetAssetDirectoryPath() / metadata.Path;
}

std::filesystem::path AssetRegistry::GetRelativePath(const std::filesystem::path& path) const {
	const auto& assetDirectoryPath = Engine::Get()->GetProject()->GetAssetDirectoryPath();
	const std::string pathStr      = path.string();
	if (pathStr.find(assetDirectoryPath.string()) != std::string::npos) {
		return std::filesystem::relative(path, assetDirectoryPath);
	} else {
		return path;
	}
}

std::filesystem::path AssetRegistry::GetPathKey(const std::filesystem::path& path) const {
	auto key = std::filesystem::relative(path, Engine::Get()->GetProject()->GetAssetDirectoryPath());
	if (key.empty()) { key = path.lexically_normal(); }

	return key;
}

bool AssetRegistry::Contains(const std::filesystem::path& path) const {
	return _assets.find(GetPathKey(path)) != _assets.end();
}

void AssetRegistry::Clear() {
	_assets.clear();
}

void AssetRegistry::Load(const std::filesystem::path& registryPath) {
	if (!std::filesystem::exists(registryPath)) {
		Log::Warning("AssetRegistry", "Asset Registry could not be found. Creating new registry.");
		_loadedRegistryPath = registryPath;
		Save();
	}

	json registryData;
	try {
		std::ifstream registryFile(registryPath);
		registryFile >> registryData;
		registryFile.close();

		json assets = registryData["assets"];
		for (const auto& assetData : assets) {
			const UUID assetUuid                    = UUID(assetData["Handle"].get<std::string>());
			const std::filesystem::path assetPath   = assetData["Path"].get<std::string>();
			const AssetType assetType               = AssetTypeFromString(assetData["Type"].get<std::string>());
			const std::filesystem::path assetSource = assetData["Source"].get<std::string>();

			AssetMetadata metadata{.Handle = assetUuid, .Type = assetType, .Path = assetPath, .Source = assetSource};
			if (std::filesystem::exists(GetFilesystemPath(metadata))) {
				_assets[assetPath] = metadata;
				Log::Trace("AssetRegistry", "Registered {} asset '{}'.", AssetTypeToString(assetType), assetPath.string());
			} else {
				Log::Warning("AssetRegistry",
				             "Failed to register {} asset '{}': File not found.",
				             AssetTypeToString(assetType),
				             assetPath.string());
				continue;
			}
		}

		Log::Info("AssetRegistry", "Registered {} assets.", _assets.size());
	} catch (const std::exception& e) { Log::Error("AssetRegistry", "Failed to load Asset Registry: {}", e.what()); }

	_loadedRegistryPath = registryPath;
}

void AssetRegistry::Remove(const std::filesystem::path& path) {
	_assets.erase(GetPathKey(path));
}

void AssetRegistry::Save() {
	if (_loadedRegistryPath.empty()) { return; }

	json registryData = json::object();
	json assets       = json::array();
	for (const auto& [assetPath, assetData] : _assets) {
		if (!std::filesystem::exists(GetFilesystemPath(assetData))) { continue; }

		const auto uuid = assetData.Handle.ToString();

		json asset      = json::object();
		asset["Handle"] = uuid;
		asset["Path"]   = assetData.Path.string();
		asset["Type"]   = AssetTypeToString(assetData.Type);
		asset["Source"] = assetData.Source.string();

		assets.push_back(asset);
	}

	registryData["assets"] = assets;

	try {
		std::ofstream registryFile(_loadedRegistryPath);
		registryFile << registryData.dump(2) << std::endl;
		registryFile.close();

		Log::Info("AssetRegistry", "Serialized {} assets.", assets.size());
	} catch (const std::exception& e) { Log::Error("AssetRegistry", "Failed to save Asset Registry: {}", e.what()); }
}

AssetMetadata& AssetRegistry::operator[](const std::filesystem::path& path) {
	return _assets[GetPathKey(path)];
}

const AssetMetadata& AssetRegistry::operator[](const std::filesystem::path& path) const {
	return _assets.at(GetPathKey(path));
}
}  // namespace Luna
