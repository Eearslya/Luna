#include <Luna/Vulkan/Buffer.hpp>
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

void CommandBuffer::CopyBuffer(const Buffer& dst, const Buffer& src) {
	CopyBuffer(dst, 0, src, 0, dst.GetCreateInfo().Size);
}

void CommandBuffer::CopyBuffer(
	const Buffer& dst, vk::DeviceSize dstOffset, const Buffer& src, vk::DeviceSize srcOffset, vk::DeviceSize size) {
	const vk::BufferCopy copy(srcOffset, dstOffset, size);
	_commandBuffer.copyBuffer(src.GetBuffer(), dst.GetBuffer(), copy);
}

void CommandBuffer::CopyBuffer(const Buffer& dst, const Buffer& src, const std::vector<vk::BufferCopy>& copies) {
	_commandBuffer.copyBuffer(src.GetBuffer(), dst.GetBuffer(), copies);
}

void CommandBuffer::FillBuffer(const Buffer& dst, uint8_t value) {
	FillBuffer(dst, value, 0, dst.GetCreateInfo().Size);
}

void CommandBuffer::FillBuffer(const Buffer& dst, uint8_t value, vk::DeviceSize offset, vk::DeviceSize size) {
	_commandBuffer.fillBuffer(dst.GetBuffer(), offset, size, value);
}
}  // namespace Vulkan
}  // namespace Luna
