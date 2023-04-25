#include <Luna/Assets/AssetFile.hpp>
#include <Luna/Assets/AssetManager.hpp>
#include <Luna/Assets/Mesh.hpp>
#include <Luna/Platform/Filesystem.hpp>
#include <Luna/Project/Project.hpp>
#include <Luna/Renderer/Renderer.hpp>
#include <Luna/Scene/Scene.hpp>
#include <Luna/Utility/Log.hpp>
#include <Luna/Utility/Serialization.hpp>
#include <Luna/Vulkan/Buffer.hpp>
#include <Luna/Vulkan/Device.hpp>
#include <nlohmann/json.hpp>

using nlohmann::json;

namespace Luna {
static bool Initialized = false;
std::unordered_map<AssetHandle, IntrusivePtr<Asset>> AssetManager::LoadedAssets;
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
	LoadedAssets.clear();
	Registry.Clear();
	Initialized = false;
}

const AssetMetadata& AssetManager::GetAssetMetadata(const Path& assetPath) {
	Path actualPath = assetPath;
	if (assetPath.IsAbsolute()) { actualPath = Path(actualPath.String().substr(1)); }

	for (auto& [handle, metadata] : Registry) {
		if (metadata.FilePath == actualPath) { return metadata; }
	}

	return NullMetadata;
}

const AssetMetadata& AssetManager::GetAssetMetadata(AssetHandle handle) {
	if (Registry.Contains(handle)) { return Registry.Get(handle); }

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

bool AssetManager::LoadAsset(const AssetMetadata& metadata, IntrusivePtr<Asset>& asset) {
	AssetFile file;
	if (!file.Load(metadata.FilePath)) {
		Log::Error(
			"AssetManager", "Failed to load asset from '{}': Could not read asset file.", metadata.FilePath.String());

		return false;
	}

	try {
		if (metadata.Type == AssetType::Mesh) {
			asset               = MakeHandle<Mesh>();
			Mesh& mesh          = *reinterpret_cast<Mesh*>(asset.Get());
			const auto meshData = json::parse(file.Json);

			mesh.Bounds           = meshData.at("Bounds").get<AABB>();
			mesh.TotalVertexCount = meshData.at("TotalVertexCount").get<vk::DeviceSize>();
			mesh.TotalIndexCount  = meshData.at("TotalIndexCount").get<vk::DeviceSize>();
			mesh.PositionSize     = meshData.at("PositionSize").get<size_t>();
			mesh.AttributeSize    = meshData.at("AttributeSize").get<size_t>();

			const auto& submeshesData = meshData.at("Submeshes");
			for (const auto& submeshData : submeshesData) {
				Mesh::Submesh submesh;
				submesh.Bounds      = submeshData.at("Bounds").get<AABB>();
				submesh.VertexCount = submeshData.at("VertexCount").get<vk::DeviceSize>();
				submesh.IndexCount  = submeshData.at("IndexCount").get<vk::DeviceSize>();
				submesh.FirstVertex = submeshData.at("FirstVertex").get<vk::DeviceSize>();
				submesh.FirstIndex  = submeshData.at("FirstIndex").get<vk::DeviceSize>();

				mesh.Submeshes.push_back(submesh);
			}

			if (file.Binary.size() > 0) {
				auto& device = Renderer::GetDevice();
				const auto posBufferCI =
					Vulkan::BufferCreateInfo(Vulkan::BufferDomain::Device,
				                           mesh.PositionSize,
				                           vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eVertexBuffer);
				mesh.PositionBuffer = device.CreateBuffer(posBufferCI, file.Binary.data());

				const auto vtxBufferCI = Vulkan::BufferCreateInfo(
					Vulkan::BufferDomain::Device, mesh.AttributeSize, vk::BufferUsageFlagBits::eVertexBuffer);
				mesh.AttributeBuffer = device.CreateBuffer(vtxBufferCI, file.Binary.data() + mesh.PositionSize);
			}

			return true;
		} else if (metadata.Type == AssetType::Scene) {
			Scene* scene = reinterpret_cast<Scene*>(asset.Get());

			return scene->Deserialize(file.Json);
		}
	} catch (const std::exception& e) {
		Log::Error("AssetManager",
		           "Failed to load {} asset from '{}': {}",
		           AssetTypeToString(metadata.Type),
		           metadata.FilePath.String(),
		           e.what());

		return false;
	}

	Log::Error("AssetManager", "Failed to load asset from '{}': Unknown asset type.", metadata.FilePath.String());

	return false;
}

void AssetManager::SaveAsset(const AssetMetadata& metadata, const IntrusivePtr<Asset>& asset) {
	if (metadata.Type == AssetType::Mesh) {
		const Mesh* mesh = reinterpret_cast<const Mesh*>(asset.Get());
		if (mesh->BufferData.empty()) { return; }

		json assetData;
		assetData["Bounds"]           = mesh->Bounds;
		assetData["TotalVertexCount"] = mesh->TotalVertexCount;
		assetData["TotalIndexCount"]  = mesh->TotalIndexCount;
		assetData["PositionSize"]     = mesh->PositionSize;
		assetData["AttributeSize"]    = mesh->AttributeSize;
		assetData["Submeshes"]        = json::array();
		for (size_t i = 0; i < mesh->Submeshes.size(); ++i) {
			const auto& submesh = mesh->Submeshes[i];
			json submeshData;
			submeshData["Bounds"]      = submesh.Bounds;
			submeshData["VertexCount"] = submesh.VertexCount;
			submeshData["IndexCount"]  = submesh.IndexCount;
			submeshData["FirstVertex"] = submesh.FirstVertex;
			submeshData["FirstIndex"]  = submesh.FirstIndex;

			assetData["Submeshes"][i] = submeshData;
		}

		AssetFile file;
		file.Type   = AssetType::Mesh;
		file.Binary = mesh->BufferData;
		file.Json   = assetData.dump();
		file.Save(metadata.FilePath);
	} else if (metadata.Type == AssetType::Scene) {
		const Scene* scene = reinterpret_cast<const Scene*>(asset.Get());

		AssetFile file;
		file.Type = AssetType::Scene;
		file.Json = scene->Serialize();
		file.Save(metadata.FilePath);
	}
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

AssetRegistry& AssetManager::GetRegistry() {
	return Registry;
}
}  // namespace Luna
