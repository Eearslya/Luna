#include "AssetManager.hpp"

#include <tiny_gltf.h>

#include <Utility/Log.hpp>
#include <Vulkan/Buffer.hpp>
#include <Vulkan/Device.hpp>
#include <Vulkan/Image.hpp>
#include <Vulkan/WSI.hpp>

#include "Editor.hpp"

using namespace Luna;

Vulkan::WSI* AssetManager::_wsi = nullptr;
std::unordered_map<std::string, std::unique_ptr<Mesh>> AssetManager::_meshes;

void AssetManager::Initialize(Luna::Vulkan::WSI& wsi) {
	_wsi = &wsi;
}

void AssetManager::Shutdown() {
	_meshes.clear();
	_wsi = nullptr;
}

Mesh* AssetManager::GetMesh(const std::filesystem::path& meshAssetPath) {
	if (meshAssetPath.empty()) { return nullptr; }

	const auto it = _meshes.find(meshAssetPath.string());
	if (it != _meshes.end()) { return it->second.get(); }

	return LoadMesh(meshAssetPath);
}

Mesh* AssetManager::LoadMesh(const std::filesystem::path& meshAssetPath) {
	std::unique_ptr<Mesh> mesh = std::make_unique<Mesh>();

	const auto gltfPath      = Editor::AssetsDirectory / meshAssetPath;
	const auto gltfFile      = gltfPath.string();
	const auto gltfFolder    = gltfPath.parent_path().string();
	const auto gltfFileName  = gltfPath.filename().string();
	const auto gltfFileNameC = gltfFileName.c_str();

	tinygltf::Model gltfModel;
	tinygltf::TinyGLTF loader;
	std::string gltfError;
	std::string gltfWarning;
	bool loaded;
	const auto gltfExt = gltfPath.extension().string();
	if (gltfExt == ".gltf") {
		loaded = loader.LoadASCIIFromFile(&gltfModel, &gltfError, &gltfWarning, gltfFile);
	} else if (gltfExt == ".glb") {
		loaded = loader.LoadBinaryFromFile(&gltfModel, &gltfError, &gltfWarning, gltfFile);
	} else {
		Log::Error("AssetManager", "Mesh asset file {} is not supported!", gltfFile);
		return nullptr;
	}

	if (!gltfError.empty()) { Log::Error("AssetManager", "Error loading mesh asset {}: {}", gltfFile, gltfError); }
	if (!gltfWarning.empty()) {
		Log::Warning("AssetManager", "Warning loading mesh asset {}: {}", gltfFile, gltfWarning);
	}
	if (!loaded) {
		Log::Error("AssetManager", "Failed to load mesh asset file {}.", gltfFile);
		return nullptr;
	}

	for (size_t i = 0; i < gltfModel.meshes.size(); ++i) {
		const auto& gltfMesh = gltfModel.meshes[i];

		struct PrimitiveContext {
			uint64_t VertexCount       = 0;
			uint64_t IndexCount        = 0;
			vk::DeviceSize FirstVertex = 0;
			vk::DeviceSize FirstIndex  = 0;
			int IndexStride            = 0;
			const void* PositionData   = nullptr;
			const void* NormalData     = nullptr;
			const void* Texcoord0Data  = nullptr;
			const void* IndexData      = nullptr;
		};

		vk::DeviceSize totalVertexCount = 0;
		vk::DeviceSize totalIndexCount  = 0;
		std::vector<PrimitiveContext> primData(gltfMesh.primitives.size());
		{
			mesh->Submeshes.resize(gltfMesh.primitives.size());
			for (size_t prim = 0; prim < gltfMesh.primitives.size(); ++prim) {
				const auto& gltfPrimitive = gltfMesh.primitives[prim];
				if (gltfPrimitive.mode != 4) {
					Log::Warning("AssetManager",
					             "{} mesh {} contains a primitive with mode {}. Only mode 4 (triangle list) is supported.",
					             gltfFile,
					             i,
					             gltfPrimitive.mode);
					continue;
				}

				auto& data = primData[prim];

				for (const auto [attributeName, attributeId] : gltfPrimitive.attributes) {
					const auto& gltfAccessor   = gltfModel.accessors[attributeId];
					const auto& gltfBufferView = gltfModel.bufferViews[gltfAccessor.bufferView];
					const auto& gltfBuffer     = gltfModel.buffers[gltfBufferView.buffer];
					const void* bufferData     = gltfBuffer.data.data() + gltfAccessor.byteOffset + gltfBufferView.byteOffset;

					if (attributeName.compare("POSITION") == 0) {
						data.VertexCount  = gltfAccessor.count;
						data.PositionData = bufferData;
					} else if (attributeName.compare("NORMAL") == 0) {
						data.NormalData = bufferData;
					} else if (attributeName.compare("TEXCOORD_0") == 0) {
						data.Texcoord0Data = bufferData;
					}
				}

				if (gltfPrimitive.indices >= 0) {
					const auto& gltfAccessor   = gltfModel.accessors[gltfPrimitive.indices];
					const auto& gltfBufferView = gltfModel.bufferViews[gltfAccessor.bufferView];
					const auto& gltfBuffer     = gltfModel.buffers[gltfBufferView.buffer];
					const void* bufferData     = gltfBuffer.data.data() + gltfAccessor.byteOffset + gltfBufferView.byteOffset;
					const auto bufferStride    = gltfAccessor.ByteStride(gltfBufferView);

					data.IndexCount  = gltfAccessor.count;
					data.IndexData   = bufferData;
					data.IndexStride = bufferStride;
				}

				data.FirstVertex = totalVertexCount;
				data.FirstIndex  = totalIndexCount;
				totalVertexCount += data.VertexCount;
				totalIndexCount += data.IndexCount;
			}
		}

		const vk::DeviceSize totalPositionSize  = ((totalVertexCount * sizeof(glm::vec3)) + 16llu) & ~16llu;
		const vk::DeviceSize totalNormalSize    = ((totalVertexCount * sizeof(glm::vec3)) + 16llu) & ~16llu;
		const vk::DeviceSize totalTexcoord0Size = ((totalVertexCount * sizeof(glm::vec2)) + 16llu) & ~16llu;
		const vk::DeviceSize totalIndexSize     = ((totalIndexCount * sizeof(uint32_t)) + 16llu) & ~16llu;
		const vk::DeviceSize bufferSize         = totalPositionSize + totalNormalSize + totalTexcoord0Size + totalIndexSize;

		mesh->PositionOffset   = 0;
		mesh->NormalOffset     = totalPositionSize;
		mesh->Texcoord0Offset  = totalPositionSize + totalNormalSize;
		mesh->IndexOffset      = totalPositionSize + totalNormalSize + totalTexcoord0Size;
		mesh->TotalVertexCount = totalVertexCount;
		mesh->TotalIndexCount  = totalIndexCount;

		std::unique_ptr<uint8_t[]> bufferData;
		bufferData.reset(new uint8_t[bufferSize]);
		uint8_t* positionCursor  = bufferData.get();
		uint8_t* normalCursor    = bufferData.get() + totalPositionSize;
		uint8_t* texcoord0Cursor = bufferData.get() + totalPositionSize + totalNormalSize;
		uint8_t* indexCursor     = bufferData.get() + totalPositionSize + totalNormalSize + totalTexcoord0Size;

		{
			for (size_t prim = 0; prim < gltfMesh.primitives.size(); ++prim) {
				const auto& data = primData[prim];
				auto& submesh    = mesh->Submeshes[prim];

				submesh.VertexCount = data.VertexCount;
				submesh.IndexCount  = data.IndexCount;
				submesh.FirstVertex = data.FirstVertex;
				submesh.FirstIndex  = data.FirstIndex;

				const size_t positionSize  = data.VertexCount * sizeof(glm::vec3);
				const size_t normalSize    = data.VertexCount * sizeof(glm::vec3);
				const size_t texcoord0Size = data.VertexCount * sizeof(glm::vec2);
				const size_t indexSize     = data.IndexCount * sizeof(uint32_t);

				memcpy(positionCursor, data.PositionData, positionSize);
				positionCursor += positionSize;

				if (data.NormalData) {
					memcpy(normalCursor, data.NormalData, normalSize);
				} else {
					memset(normalCursor, 0, normalSize);
				}
				normalCursor += normalSize;

				if (data.Texcoord0Data) {
					memcpy(texcoord0Cursor, data.Texcoord0Data, texcoord0Size);
				} else {
					memset(texcoord0Cursor, 0, texcoord0Size);
				}
				texcoord0Cursor += texcoord0Size;

				if (data.IndexData) {
					if (data.IndexStride == 1) {
						uint32_t* dst      = reinterpret_cast<uint32_t*>(indexCursor);
						const uint8_t* src = reinterpret_cast<const uint8_t*>(data.IndexData);
						for (size_t i = 0; i < data.IndexCount; ++i) { dst[i] = src[i]; }
					} else if (data.IndexStride == 2) {
						uint32_t* dst       = reinterpret_cast<uint32_t*>(indexCursor);
						const uint16_t* src = reinterpret_cast<const uint16_t*>(data.IndexData);
						for (size_t i = 0; i < data.IndexCount; ++i) { dst[i] = src[i]; }
					} else if (data.IndexStride == 4) {
						memcpy(indexCursor, data.IndexData, indexSize);
					}
				} else {
					memset(indexCursor, 0, indexSize);
				}
				indexCursor += indexSize;
			}
		}

		auto& device = _wsi->GetDevice();
		mesh->Buffer = device.CreateBuffer(
			Vulkan::BufferCreateInfo(Vulkan::BufferDomain::Device,
		                           bufferSize,
		                           vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eIndexBuffer),
			bufferData.get());
	}

	_meshes[meshAssetPath.string()] = std::move(mesh);

	return _meshes[meshAssetPath.string()].get();
}
