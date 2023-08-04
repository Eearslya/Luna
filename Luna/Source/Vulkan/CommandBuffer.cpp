#include <Luna/Vulkan/Buffer.hpp>
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

void CommandBuffer::Barrier(const vk::DependencyInfo& dependency) {
	if (_device._deviceInfo.EnabledFeatures.Synchronization2.synchronization2) {
		_commandBuffer.pipelineBarrier2KHR(dependency);
	} else {
		vk::PipelineStageFlags2 srcStages;
		vk::PipelineStageFlags2 dstStages;
		std::vector<vk::MemoryBarrier> memoryBarriers(dependency.memoryBarrierCount);
		std::vector<vk::BufferMemoryBarrier> bufferBarriers(dependency.bufferMemoryBarrierCount);
		std::vector<vk::ImageMemoryBarrier> imageBarriers(dependency.imageMemoryBarrierCount);

		for (uint32_t i = 0; i < dependency.memoryBarrierCount; ++i) {
			const auto& barrier = dependency.pMemoryBarriers[i];
			srcStages |= barrier.srcStageMask;
			dstStages |= barrier.dstStageMask;
			memoryBarriers[i] =
				vk::MemoryBarrier(DowngradeAccessFlags2(barrier.srcAccessMask), DowngradeAccessFlags2(barrier.dstAccessMask));
		}

		for (uint32_t i = 0; i < dependency.bufferMemoryBarrierCount; ++i) {
			const auto& barrier = dependency.pBufferMemoryBarriers[i];
			srcStages |= barrier.srcStageMask;
			dstStages |= barrier.dstStageMask;
			bufferBarriers[i] = vk::BufferMemoryBarrier(DowngradeAccessFlags2(barrier.srcAccessMask),
			                                            DowngradeAccessFlags2(barrier.dstAccessMask),
			                                            barrier.srcQueueFamilyIndex,
			                                            barrier.dstQueueFamilyIndex,
			                                            barrier.buffer,
			                                            barrier.offset,
			                                            barrier.size);
		}

		for (uint32_t i = 0; i < dependency.imageMemoryBarrierCount; ++i) {
			const auto& barrier = dependency.pImageMemoryBarriers[i];
			srcStages |= barrier.srcStageMask;
			dstStages |= barrier.dstStageMask;
			imageBarriers[i] = vk::ImageMemoryBarrier(DowngradeAccessFlags2(barrier.srcAccessMask),
			                                          DowngradeAccessFlags2(barrier.dstAccessMask),
			                                          barrier.oldLayout,
			                                          barrier.newLayout,
			                                          barrier.srcQueueFamilyIndex,
			                                          barrier.dstQueueFamilyIndex,
			                                          barrier.image,
			                                          barrier.subresourceRange);
		}

		_commandBuffer.pipelineBarrier(DowngradeSrcPipelineStageFlags2(srcStages),
		                               DowngradeDstPipelineStageFlags2(dstStages),
		                               dependency.dependencyFlags,
		                               memoryBarriers,
		                               bufferBarriers,
		                               imageBarriers);
	}
}

void CommandBuffer::BufferBarrier(const Buffer& buffer,
                                  vk::PipelineStageFlags2 srcStages,
                                  vk::AccessFlags2 srcAccess,
                                  vk::PipelineStageFlags2 dstStages,
                                  vk::AccessFlags2 dstAccess) {
	const vk::BufferMemoryBarrier2 barrier(srcStages,
	                                       srcAccess,
	                                       dstStages,
	                                       dstAccess,
	                                       VK_QUEUE_FAMILY_IGNORED,
	                                       VK_QUEUE_FAMILY_IGNORED,
	                                       buffer.GetBuffer(),
	                                       0,
	                                       VK_WHOLE_SIZE);
	const vk::DependencyInfo dependency({}, nullptr, barrier, nullptr);
	Barrier(dependency);
}

void CommandBuffer::CopyBuffer(const Buffer& dst, const Buffer& src) {
	CopyBuffer(dst, 0, src, 0, src.GetCreateInfo().Size);
}

void CommandBuffer::CopyBuffer(const Buffer& dst, const Buffer& src, const std::vector<vk::BufferCopy>& copies) {
	_commandBuffer.copyBuffer(src.GetBuffer(), dst.GetBuffer(), copies);
}

void CommandBuffer::CopyBuffer(
	const Buffer& dst, vk::DeviceSize dstOffset, const Buffer& src, vk::DeviceSize srcOffset, vk::DeviceSize size) {
	const vk::BufferCopy copy(srcOffset, dstOffset, size);
	_commandBuffer.copyBuffer(src.GetBuffer(), dst.GetBuffer(), copy);
}

void CommandBuffer::FillBuffer(const Buffer& dst, uint8_t value) {
	FillBuffer(dst, value, 0, VK_WHOLE_SIZE);
}

void CommandBuffer::FillBuffer(const Buffer& dst, uint8_t value, vk::DeviceSize offset, vk::DeviceSize size) {
	_commandBuffer.fillBuffer(dst.GetBuffer(), offset, size, value);
}
}  // namespace Vulkan
}  // namespace Luna
