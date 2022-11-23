#include <Luna/Vulkan/CommandBuffer.hpp>
#include <Luna/Vulkan/Device.hpp>

namespace Luna {
namespace Vulkan {
void CommandBufferDeleter::operator()(CommandBuffer* cmdBuf) {
	cmdBuf->_device._commandBufferPool.Free(cmdBuf);
}

CommandBuffer::CommandBuffer(Device& device, vk::CommandBuffer cmdBuf, CommandBufferType type, uint32_t threadIndex)
		: _device(device), _commandBuffer(cmdBuf), _commandBufferType(type), _threadIndex(threadIndex) {}

CommandBuffer::~CommandBuffer() noexcept {}

void CommandBuffer::End() {
	_commandBuffer.end();
}
}  // namespace Vulkan
}  // namespace Luna
