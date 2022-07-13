#include "Buffer.hpp"

#include "Device.hpp"

namespace Luna {
namespace Vulkan {
void BufferDeleter::operator()(Buffer* buffer) {
	buffer->_device._bufferPool.Free(buffer);
}

Buffer::Buffer(Device& device,
               vk::Buffer buffer,
               const VmaAllocation& allocation,
               const BufferCreateInfo& bufferCI,
               void* mappedMemory)
		: Cookie(device),
			_device(device),
			_buffer(buffer),
			_allocation(allocation),
			_createInfo(bufferCI),
			_mappedMemory(mappedMemory) {}

Buffer::~Buffer() noexcept {
	if (_internalSync) {
		_device.DestroyBufferNoLock(_buffer);
		_device.FreeMemoryNoLock(_allocation);
	} else {
		_device.DestroyBuffer(_buffer);
		_device.FreeMemory(_allocation);
	}
}
}  // namespace Vulkan
}  // namespace Luna
