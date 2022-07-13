#include "CommandBuffer.hpp"

#include "Buffer.hpp"
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

void CommandBuffer::End() {
	_commandBuffer.end();
}

void CommandBuffer::Barrier(vk::PipelineStageFlags srcStage,
                            vk::AccessFlags srcAccess,
                            vk::PipelineStageFlags dstStage,
                            vk::AccessFlags dstAccess) {
	const vk::MemoryBarrier barrier(srcAccess, dstAccess);
	_commandBuffer.pipelineBarrier(srcStage, dstStage, {}, barrier, nullptr, nullptr);
}

void CommandBuffer::CopyBuffer(const Buffer& dst, const Buffer& src) {
	CopyBuffer(dst, 0, src, 0, dst.GetCreateInfo().Size);
}

void CommandBuffer::CopyBuffer(
	const Buffer& dst, vk::DeviceSize dstOffset, const Buffer& src, vk::DeviceSize srcOffset, vk::DeviceSize size) {
	const vk::BufferCopy copy(srcOffset, dstOffset, size);
	_commandBuffer.copyBuffer(src.GetBuffer(), dst.GetBuffer(), copy);
}
}  // namespace Vulkan
}  // namespace Luna
