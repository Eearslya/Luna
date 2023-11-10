#include <Luna/Vulkan/Buffer.hpp>
#include <Luna/Vulkan/CommandBuffer.hpp>
#include <Luna/Vulkan/Device.hpp>
#include <Luna/Vulkan/Image.hpp>

namespace Luna {
namespace Vulkan {
Hash DeferredPipelineCompile::GetComputeHash() const {
	return {};
}

Hash DeferredPipelineCompile::GetHash(uint32_t& activeVBOs) const {
	return {};
}

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

void CommandBuffer::BeginRenderPass(const RenderPassInfo& rpInfo, vk::SubpassContents contents) {
	_framebuffer                        = &_device.RequestFramebuffer(rpInfo);
	_pipelineState.CompatibleRenderPass = &_framebuffer->GetCompatibleRenderPass();
	_actualRenderPass                   = &_device.RequestRenderPass(rpInfo, false);
	_pipelineState.SubpassIndex         = 0;

	_framebufferAttachments.fill(nullptr);
	for (uint32_t i = 0; i < rpInfo.ColorAttachmentCount; ++i) {
		_framebufferAttachments[i] = rpInfo.ColorAttachments[i];
	}
	if (rpInfo.DepthStencilAttachment) {
		_framebufferAttachments[rpInfo.ColorAttachmentCount] = rpInfo.DepthStencilAttachment;
	}

	SetViewportScissor(rpInfo, _framebuffer);

	uint32_t clearValueCount = 0;
	std::array<vk::ClearValue, MaxColorAttachments + 1> clearValues;
	for (uint32_t i = 0; i < rpInfo.ColorAttachmentCount; ++i) {
		if (rpInfo.ClearAttachmentMask & (1u << i)) {
			clearValues[i].color = rpInfo.ClearColors[i];
			clearValueCount      = i + 1;
		}

		if (rpInfo.ColorAttachments[i]->GetImage().IsSwapchainImage()) {
			_swapchainStages |= vk::PipelineStageFlagBits2::eColorAttachmentOutput;
		}
	}
	if (rpInfo.DepthStencilAttachment && (rpInfo.Flags & RenderPassFlagBits::ClearDepthStencil)) {
		clearValues[rpInfo.ColorAttachmentCount].depthStencil = rpInfo.ClearDepthStencil;
		clearValueCount                                       = rpInfo.ColorAttachmentCount + 1;
	}

	const vk::RenderPassBeginInfo rpBI(
		_actualRenderPass->GetRenderPass(), _framebuffer->GetFramebuffer(), _scissor, clearValueCount, clearValues.data());
	_commandBuffer.beginRenderPass(rpBI, contents);
	_currentContents = contents;

	BeginGraphics();
}

void CommandBuffer::NextSubpass(vk::SubpassContents contents) {
	_pipelineState.SubpassIndex++;
	_commandBuffer.nextSubpass(contents);
	_currentContents = contents;

	BeginGraphics();
}

void CommandBuffer::EndRenderPass() {
	_commandBuffer.endRenderPass();

	_framebuffer                        = nullptr;
	_actualRenderPass                   = nullptr;
	_pipelineState.CompatibleRenderPass = nullptr;

	BeginCompute();
}

void CommandBuffer::BeginCompute() {
	_isCompute = true;
	BeginContext();
}

void CommandBuffer::BeginContext() {
	_dirty     = ~0u;
	_dirtySets = ~0u;
	_dirtyVBOs = ~0u;
	// _currentPipeline.Pipeline = nullptr;
	_pipelineLayout = nullptr;
	// _programLayout = nullptr;
	// _pipelineState.Program = nullptr;
	_pipelineState.PotentialStaticState.SpecConstantMask         = 0;
	_pipelineState.PotentialStaticState.InternalSpecConstantMask = 0;
	for (uint32_t set = 0; set < MaxDescriptorSets; ++set) {
		for (uint32_t binding = 0; binding < MaxDescriptorBindings; ++binding) {
			// _bindings.Bindings[set][binding].Cookie = 0;
			// _bindings.Bindings[set][binding].SecondaryCookie = 0;
		}
	}
	_vertexBindings.Buffers.fill(VK_NULL_HANDLE);
	memset(&_indexState, 0, sizeof(_indexState));
}

void CommandBuffer::BeginGraphics() {
	_isCompute = false;
	BeginContext();
}

void CommandBuffer::SetViewportScissor(const RenderPassInfo& rpInfo, const Framebuffer* framebuffer) {
	vk::Rect2D rect     = rpInfo.RenderArea;
	vk::Extent2D extent = framebuffer->GetExtent();

	rect.offset.x      = std::min(int32_t(extent.width), rect.offset.x);
	rect.offset.y      = std::min(int32_t(extent.height), rect.offset.y);
	rect.extent.width  = std::min(extent.width - rect.offset.x, rect.extent.width);
	rect.extent.height = std::min(extent.height - rect.offset.y, rect.extent.height);

	// Note: Viewport is flipped up-side-down here for compatibility with GLM matrices.
	_viewport = vk::Viewport(
		float(rect.offset.x), float(rect.extent.height), float(rect.extent.width), -float(rect.extent.height), 0.0f, 1.0f);
	_scissor = rect;
}
}  // namespace Vulkan
}  // namespace Luna
