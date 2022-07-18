#pragma once

#include <vector>

#include "Vulkan/Common.hpp"

struct Submesh {
	vk::DeviceSize VertexCount = 0;
	vk::DeviceSize IndexCount  = 0;
	vk::DeviceSize FirstVertex = 0;
	vk::DeviceSize FirstIndex  = 0;
};

struct Mesh {
	std::vector<Submesh> Submeshes;
	Luna::Vulkan::BufferHandle Buffer;
	vk::DeviceSize PositionOffset   = 0;
	vk::DeviceSize NormalOffset     = 0;
	vk::DeviceSize Texcoord0Offset  = 0;
	vk::DeviceSize IndexOffset      = 0;
	vk::DeviceSize TotalVertexCount = 0;
	vk::DeviceSize TotalIndexCount  = 0;
};
