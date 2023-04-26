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

	struct Vertex {
		glm::vec3 Normal    = glm::vec3(0.0f);
		glm::vec4 Tangent   = glm::vec4(0.0f);
		glm::vec2 Texcoord0 = glm::vec2(0.0f);
		glm::vec2 Texcoord1 = glm::vec2(0.0f);
		glm::vec4 Color0    = glm::vec4(0.0f);
		glm::uvec4 Joints0  = glm::uvec4(0);
		glm::vec4 Weights0  = glm::vec4(0.0f);
	};

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
