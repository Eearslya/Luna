#include <Luna/Assets/AssetFile.hpp>
#include <Luna/Assets/AssetManager.hpp>
#include <Luna/Assets/Material.hpp>
#include <Luna/Assets/Mesh.hpp>
#include <Luna/Platform/Filesystem.hpp>
#include <Luna/Project/Project.hpp>
#include <Luna/Renderer/Renderer.hpp>
#include <Luna/Scene/Scene.hpp>
#include <Luna/Utility/Log.hpp>
#include <Luna/Utility/Serialization.hpp>
#include <Luna/Utility/Threading.hpp>
#include <Luna/Vulkan/Buffer.hpp>
#include <Luna/Vulkan/Device.hpp>
#include <nlohmann/json.hpp>

using nlohmann::json;

namespace Luna {
static bool Initialized = false;
std::unordered_map<AssetHandle, IntrusivePtr<Asset>> AssetManager::LoadedAssets;
AssetRegistry AssetManager::Registry = {};
std::mutex AssetManager::AsyncLock;
std::vector<AssetHandle> AssetManager::AsyncRequests;

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
	std::lock_guard<std::mutex> lock(AsyncLock);
	if (!AsyncRequests.empty()) { Threading::WaitIdle(); }

	SaveRegistry();
	LoadedAssets.clear();
	Registry.Clear();
	Initialized = false;
}

void AssetManager::EnqueueAssetLoad(const AssetMetadata& metadata) {
	std::lock_guard<std::mutex> lock(AsyncLock);

	const auto it = std::find(AsyncRequests.begin(), AsyncRequests.end(), metadata.Handle);
	if (it != AsyncRequests.end()) { return; }
	AsyncRequests.push_back(metadata.Handle);

	auto group = Threading::CreateTaskGroup();
	group->Enqueue([metadata]() {
		IntrusivePtr<Asset> asset;
		if (LoadAsset(metadata, asset)) { LoadedAssets[metadata.Handle] = asset; }

		std::lock_guard<std::mutex> lock(AsyncLock);
		const auto it = std::find(AsyncRequests.begin(), AsyncRequests.end(), metadata.Handle);
		if (it != AsyncRequests.end()) { AsyncRequests.erase(it); }
	});
	group->Flush();
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
			asset        = MakeHandle<Scene>();
			Scene* scene = reinterpret_cast<Scene*>(asset.Get());

			return scene->Deserialize(file.Json);
		} else if (metadata.Type == AssetType::Material) {
			asset                   = MakeHandle<Material>();
			Material& material      = *reinterpret_cast<Material*>(asset.Get());
			const auto materialData = json::parse(file.Json);

			material.BaseColorFactor = materialData.at("BaseColorFactor").get<glm::vec4>();
			material.EmissiveFactor  = materialData.at("EmissiveFactor").get<glm::vec3>();
			material.AlphaCutoff     = materialData.at("AlphaCutoff").get<float>();
			material.MetallicFactor  = materialData.at("MetallicFactor").get<float>();
			material.RoughnessFactor = materialData.at("RoughnessFactor").get<float>();

			return true;
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

void AssetManager::RenameAsset(const AssetMetadata& metadata, const std::string& newName) {
	if (!metadata.IsValid()) { return; }

	auto& meta         = Registry.Get(metadata.Handle);
	const auto baseDir = meta.FilePath.BaseDirectory();
	const auto newPath = baseDir / newName;
	auto* backend      = Filesystem::GetBackend("project");
	if (backend->MoveYield(newPath, metadata.FilePath)) { meta.FilePath = newPath; }
}

bool AssetManager::SaveAsset(const AssetMetadata& metadata, const IntrusivePtr<Asset>& asset) {
	if (metadata.Type == AssetType::Mesh) {
		const Mesh* mesh = reinterpret_cast<const Mesh*>(asset.Get());
		if (mesh->BufferData.empty()) { return true; }

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
		return file.Save(metadata.FilePath);
	} else if (metadata.Type == AssetType::Scene) {
		const Scene* scene = reinterpret_cast<const Scene*>(asset.Get());

		AssetFile file;
		file.Type = AssetType::Scene;
		file.Json = scene->Serialize();
		return file.Save(metadata.FilePath);
	} else if (metadata.Type == AssetType::Material) {
		const Material* material = reinterpret_cast<const Material*>(asset.Get());

		json materialData;
		materialData["BaseColorFactor"] = material->BaseColorFactor;
		materialData["EmissiveFactor"]  = material->EmissiveFactor;
		materialData["AlphaCutoff"]     = material->AlphaCutoff;
		materialData["MetallicFactor"]  = material->MetallicFactor;
		materialData["RoughnessFactor"] = material->RoughnessFactor;

		AssetFile file;
		file.Type = AssetType::Material;
		file.Json = materialData.dump();
		return file.Save(metadata.FilePath);
	}

	return false;
}

void AssetManager::SaveLoaded() {
	for (auto& asset : LoadedAssets) {
		const auto& metadata = GetAssetMetadata(asset.first);
		SaveAsset(metadata, asset.second);
	}
}

void AssetManager::UnloadAsset(const AssetMetadata& metadata) {
	if (metadata.IsValid() && LoadedAssets.find(metadata.Handle) != LoadedAssets.end()) {
		LoadedAssets.erase(metadata.Handle);
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

	try {
		const auto registryData = json::parse(registryJson);
		const auto& assetsData  = registryData.at("Assets");
		for (const auto& assetData : assetsData) {
			try {
				const auto filePath      = assetData.at("FilePath").get<std::string>();
				const AssetHandle handle = assetData.at("Handle").get<uint64_t>();
				const AssetType type     = AssetTypeFromString(assetData.at("Type").get<std::string>());
				const AssetMetadata metadata{.FilePath = filePath, .Handle = handle, .Type = type};

				if (type == AssetType::None) { continue; }
				if (!Filesystem::Exists(GetFilesystemPath(metadata))) { continue; }
				if (uint64_t(handle) == 0) { continue; }

				Registry[metadata.Handle] = metadata;
			} catch (const std::exception& e) {
				Log::Warning("AssetManager", "Encountered malformed asset in Asset Registry. Ignoring.");
			}
		}
	} catch (const std::exception& e) { Log::Error("AssetManager", "Failed to load Asset Registry!"); }
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
