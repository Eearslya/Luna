#include <Luna/Vulkan/CommandBuffer.hpp>
#include <Luna/Vulkan/Device.hpp>

namespace Luna {
namespace Vulkan {
void CommandBufferDeleter::operator()(CommandBuffer* commandBuffer) {
	commandBuffer->_device._commandBufferPool.Free(commandBuffer);
}

CommandBuffer::CommandBuffer(Device& device,
                             CommandBufferType type,
                             vk::CommandBuffer commandBuffer,
                             uint32_t threadIndex,
                             const std::string& debugName)
		: _device(device), _type(type), _commandBuffer(commandBuffer), _threadIndex(threadIndex), _debugName(debugName) {
	_device.SetObjectName(_commandBuffer, debugName);
}

CommandBuffer::~CommandBuffer() noexcept {}

void CommandBuffer::Begin() {
	const vk::CommandBufferBeginInfo beginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit, nullptr);
	_commandBuffer.begin(beginInfo);
}

void CommandBuffer::End() {
	_commandBuffer.end();
}
}  // namespace Vulkan
}  // namespace Luna
