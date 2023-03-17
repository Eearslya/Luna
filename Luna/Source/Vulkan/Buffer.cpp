#include <Luna/Vulkan/Buffer.hpp>
#include <Luna/Vulkan/Device.hpp>

namespace Luna {
namespace Vulkan {
vk::AccessFlags BufferUsageToAccess(vk::BufferUsageFlags usage) {
	vk::AccessFlags access = {};

	if (usage & (vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eTransferSrc)) {
		access |= vk::AccessFlagBits::eTransferRead | vk::AccessFlagBits::eTransferWrite;
	}
	if (usage & vk::BufferUsageFlagBits::eVertexBuffer) { access |= vk::AccessFlagBits::eVertexAttributeRead; }
	if (usage & vk::BufferUsageFlagBits::eIndexBuffer) { access |= vk::AccessFlagBits::eIndexRead; }
	if (usage & vk::BufferUsageFlagBits::eIndirectBuffer) { access |= vk::AccessFlagBits::eIndirectCommandRead; }
	if (usage & vk::BufferUsageFlagBits::eUniformBuffer) { access |= vk::AccessFlagBits::eUniformRead; }
	if (usage & vk::BufferUsageFlagBits::eStorageBuffer) {
		access |= vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite;
	}
	if (usage & vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR) {
		access |= vk::AccessFlagBits::eAccelerationStructureReadKHR;
	}

	return access;
}

vk::PipelineStageFlags BufferUsageToStages(vk::BufferUsageFlags usage) {
	vk::PipelineStageFlags stages = {};

	if (usage & (vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eTransferSrc)) {
		stages |= vk::PipelineStageFlagBits::eTransfer;
	}
	if (usage & (vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eIndexBuffer)) {
		stages |= vk::PipelineStageFlagBits::eVertexInput;
	}
	if (usage & vk::BufferUsageFlagBits::eIndirectBuffer) { stages |= vk::PipelineStageFlagBits::eDrawIndirect; }
	if (usage & (vk::BufferUsageFlagBits::eStorageTexelBuffer | vk::BufferUsageFlagBits::eUniformBuffer |
	             vk::BufferUsageFlagBits::eUniformTexelBuffer)) {
		stages |= vk::PipelineStageFlagBits::eComputeShader | vk::PipelineStageFlagBits::eFragmentShader |
		          vk::PipelineStageFlagBits::eVertexShader;
	}
	if (usage & vk::BufferUsageFlagBits::eStorageBuffer) { stages |= vk::PipelineStageFlagBits::eComputeShader; }

	return stages;
}

void BufferDeleter::operator()(Buffer* buffer) {
	buffer->_device._bufferPool.Free(buffer);
}

Buffer::Buffer(Device& device,
               vk::Buffer buffer,
               const VmaAllocation& allocation,
               const BufferCreateInfo& createInfo,
               void* mappedMemory)
		: Cookie(device),
			_device(device),
			_buffer(buffer),
			_allocation(allocation),
			_createInfo(createInfo),
			_mappedMemory(mappedMemory) {
	if (createInfo.Usage & vk::BufferUsageFlagBits::eShaderDeviceAddress) {
		const vk::BufferDeviceAddressInfo addressInfo(_buffer);
		_deviceAddress = _device._device.getBufferAddress(addressInfo);
	}
}

Buffer::~Buffer() noexcept {
	if (_mappedMemory) { vmaUnmapMemory(_device._allocator, _allocation); }
	if (_internalSync) {
		_device.DestroyBufferNoLock(_buffer);
		_device.FreeAllocationNoLock(_allocation);
	} else {
		_device.DestroyBuffer(_buffer);
		_device.FreeAllocation(_allocation);
	}
}
}  // namespace Vulkan
}  // namespace Luna
