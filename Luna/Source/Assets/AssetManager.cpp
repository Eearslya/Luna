#include <Luna/Assets/AssetManager.hpp>
#include <Luna/Platform/Filesystem.hpp>
#include <Luna/Project/Project.hpp>
#include <nlohmann/json.hpp>

using nlohmann::json;

namespace Luna {
static bool Initialized              = false;
AssetRegistry AssetManager::Registry = {};

static const AssetMetadata NullMetadata = {};

void AssetManager::Initialize() {
	if (Initialized) { Shutdown(); }

	auto project = Project::GetActive();
	if (!project) { return; }

	LoadRegistry();
	LoadAssets();

	Initialized = true;
}

void AssetManager::Shutdown() {
	SaveRegistry();
	Registry.Clear();
	Initialized = false;
}

const AssetMetadata& AssetManager::GetAssetMetadata(const Path& assetPath) {
	for (auto& [handle, metadata] : Registry) {
		if (metadata.FilePath == assetPath) { return metadata; }
	}

	return NullMetadata;
}

Path AssetManager::GetFilesystemPath(const AssetMetadata& metadata) {
	return Path("project://") / metadata.FilePath;
}

AssetHandle AssetManager::ImportAsset(const Path& assetPath) {
	if (auto& metadata = GetAssetMetadata(assetPath); metadata.IsValid()) { return metadata.Handle; }

	const auto type = AssetTypeFromPath(assetPath);
	if (type == AssetType::None) { return 0; }

	AssetMetadata metadata    = {.FilePath = assetPath, .Handle = AssetHandle(), .Type = type};
	Registry[metadata.Handle] = metadata;

	return metadata.Handle;
}

void AssetManager::LoadAssets() {
	for (auto& entry : Filesystem::Walk("project://")) {
		if (entry.Type != PathType::File) { continue; }
		ImportAsset(entry.Path.WithoutProtocol());
	}

	SaveRegistry();
}

void AssetManager::LoadRegistry() {
	if (!Filesystem::Exists("project://AssetRegistry.lregistry")) { return; }

	std::string registryJson;
	if (!Filesystem::ReadFileToString("project://AssetRegistry.lregistry", registryJson)) { return; }
}

void AssetManager::SaveRegistry() {
	struct AssetRegistryEntry {
		Path FilePath;
		AssetType Type;
	};
	std::map<UUID, AssetRegistryEntry> assetMap;
	for (auto& [handle, metadata] : Registry) {
		if (!Filesystem::Exists(GetFilesystemPath(metadata))) { continue; }
		assetMap[metadata.Handle] = {metadata.FilePath, metadata.Type};
	}

	json data;
	data["Assets"] = json::array();
	auto& assets   = data["Assets"];
	for (auto& [handle, entry] : assetMap) {
		json asset;
		asset["Handle"]   = uint64_t(handle);
		asset["FilePath"] = entry.FilePath.String();
		asset["Type"]     = AssetTypeToString(entry.Type);
		assets.push_back(asset);
	}

	Filesystem::WriteStringToFile("project://AssetRegistry.lregistry", data.dump());
}
}  // namespace Luna
