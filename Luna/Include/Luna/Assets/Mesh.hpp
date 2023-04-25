#pragma once

#include <Luna/Assets/Asset.hpp>
#include <Luna/Utility/AABB.hpp>
#include <Luna/Vulkan/Buffer.hpp>
#include <vector>

namespace Luna {
class Mesh : public Asset {
 public:
	static AssetType GetAssetType() {
		return AssetType::Mesh;
	}

	struct Submesh {
		AABB Bounds;
		vk::DeviceSize VertexCount = 0;
		vk::DeviceSize IndexCount  = 0;
		vk::DeviceSize FirstVertex = 0;
		vk::DeviceSize FirstIndex  = 0;
	};

	// Serialized Data
	AABB Bounds;
	std::vector<Submesh> Submeshes;
	vk::DeviceSize TotalVertexCount = 0;
	vk::DeviceSize TotalIndexCount  = 0;

	size_t PositionSize  = 0;
	size_t AttributeSize = 0;

	// Unserialized Data
	Vulkan::BufferHandle PositionBuffer;
	Vulkan::BufferHandle AttributeBuffer;
	std::vector<uint8_t> BufferData;
};
}  // namespace Luna
