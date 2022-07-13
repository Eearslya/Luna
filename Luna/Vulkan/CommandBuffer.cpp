#include "CommandBuffer.hpp"

#include "Device.hpp"

namespace Luna {
namespace Vulkan {
void CommandBufferDeleter::operator()(CommandBuffer* commandBuffer) {}

CommandBuffer::CommandBuffer(Device& device,
                             vk::CommandBuffer commandBuffer,
                             CommandBufferType type,
                             uint32_t threadIndex)
		: _device(device), _commandBuffer(commandBuffer), _type(type), _threadIndex(threadIndex) {}

CommandBuffer::~CommandBuffer() noexcept {}
}  // namespace Vulkan
}  // namespace Luna
