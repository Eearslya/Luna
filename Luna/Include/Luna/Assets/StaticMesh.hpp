#pragma once

#include <Luna/Utility/IntrusivePtr.hpp>
#include <Luna/Vulkan/Common.hpp>

namespace Luna {
struct StaticMesh;

struct StaticMeshDeleter {
	void operator()(StaticMesh* mesh);
};

struct SubMesh {
	vk::DeviceSize VertexCount = 0;
	vk::DeviceSize IndexCount  = 0;
	vk::DeviceSize FirstVertex = 0;
	vk::DeviceSize FirstIndex  = 0;
};

struct StaticMesh : public IntrusivePtrEnabled<StaticMesh, StaticMeshDeleter, MultiThreadCounter> {
	StaticMesh();
	~StaticMesh() noexcept;

	std::vector<SubMesh> SubMeshes;
	Vulkan::BufferHandle Buffer;
	vk::DeviceSize PositionOffset  = 0;
	vk::DeviceSize NormalOffset    = 0;
	vk::DeviceSize Texcoord0Offset = 0;
	vk::DeviceSize IndexOffset     = 0;
	std::atomic_bool Ready         = false;
};

using StaticMeshHandle = IntrusivePtr<StaticMesh>;
}  // namespace Luna
