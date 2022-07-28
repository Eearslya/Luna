#pragma once

#include <vector>

#include "Utility/AABB.hpp"
#include "Utility/IntrusivePtr.hpp"
#include "Vulkan/Buffer.hpp"

namespace Luna {
struct Submesh {
	AABB Bounds;
	vk::DeviceSize VertexCount = 0;
	vk::DeviceSize IndexCount  = 0;
	vk::DeviceSize FirstVertex = 0;
	vk::DeviceSize FirstIndex  = 0;
	uint32_t MaterialIndex     = 0;
};

struct Mesh : public IntrusivePtrEnabled<Mesh> {
	AABB Bounds;
	std::vector<Submesh> Submeshes;
	Luna::Vulkan::BufferHandle Buffer;
	vk::DeviceSize PositionOffset   = 0;
	vk::DeviceSize NormalOffset     = 0;
	vk::DeviceSize TangentOffset    = 0;
	vk::DeviceSize BitangentOffset  = 0;
	vk::DeviceSize Texcoord0Offset  = 0;
	vk::DeviceSize IndexOffset      = 0;
	vk::DeviceSize TotalVertexCount = 0;
	vk::DeviceSize TotalIndexCount  = 0;
};
}  // namespace Luna
