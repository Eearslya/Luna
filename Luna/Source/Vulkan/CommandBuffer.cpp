#include <Luna/Vulkan/CommandBuffer.hpp>
#include <Luna/Vulkan/Device.hpp>

namespace Luna {
namespace Vulkan {
void CommandBufferDeleter::operator()(CommandBuffer* buffer) {
	buffer->_device.ReleaseCommandBuffer({}, buffer);
}

CommandBuffer::CommandBuffer(Device& device,
                             vk::CommandBuffer commandBuffer,
                             CommandBufferType type,
                             uint32_t threadIndex)
		: _device(device), _commandBuffer(commandBuffer), _commandBufferType(type), _threadIndex(threadIndex) {}

CommandBuffer::~CommandBuffer() noexcept {}

void CommandBuffer::Begin() {
	const vk::CommandBufferBeginInfo cmdBI(vk::CommandBufferUsageFlagBits::eOneTimeSubmit, nullptr);
	_commandBuffer.begin(cmdBI);
}

void CommandBuffer::End() {
	_commandBuffer.end();
}
}  // namespace Vulkan
}  // namespace Luna
