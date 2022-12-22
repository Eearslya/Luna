#include <Luna/Vulkan/Buffer.hpp>
#include <Luna/Vulkan/CommandBuffer.hpp>
#include <Luna/Vulkan/Device.hpp>
#include <Luna/Vulkan/Image.hpp>

namespace Luna {
namespace Vulkan {
void CommandBufferDeleter::operator()(CommandBuffer* cmdBuf) {
	cmdBuf->_device._commandBufferPool.Free(cmdBuf);
}

CommandBuffer::CommandBuffer(Device& device, vk::CommandBuffer cmdBuf, CommandBufferType type, uint32_t threadIndex)
		: _device(device), _commandBuffer(cmdBuf), _commandBufferType(type), _threadIndex(threadIndex) {
	BeginCompute();
	SetOpaqueState();
}

CommandBuffer::~CommandBuffer() noexcept {}

void CommandBuffer::End() {
	_commandBuffer.end();
}

void CommandBuffer::Barrier(vk::PipelineStageFlags srcStages,
                            vk::AccessFlags srcAccess,
                            vk::PipelineStageFlags dstStages,
                            vk::AccessFlags dstAccess) {
	const vk::MemoryBarrier barrier(srcAccess, dstAccess);
	_commandBuffer.pipelineBarrier(srcStages, dstStages, {}, barrier, nullptr, nullptr);
}

void CommandBuffer::Barrier(vk::PipelineStageFlags srcStages,
                            vk::PipelineStageFlags dstStages,
                            const std::vector<vk::MemoryBarrier>& memoryBarriers,
                            const std::vector<vk::BufferMemoryBarrier>& bufferBarriers,
                            const std::vector<vk::ImageMemoryBarrier>& imageBarriers) {
	_commandBuffer.pipelineBarrier(srcStages, dstStages, {}, memoryBarriers, bufferBarriers, imageBarriers);
}

void CommandBuffer::BarrierPrepareGenerateMipmaps(const Image& image,
                                                  vk::ImageLayout baseLevelLayout,
                                                  vk::PipelineStageFlags srcStages,
                                                  vk::AccessFlags srcAccess,
                                                  bool needTopLevelBarrier) {
	const auto& createInfo = image.GetCreateInfo();

	const vk::ImageMemoryBarrier top(
		srcAccess,
		vk::AccessFlagBits::eTransferRead,
		baseLevelLayout,
		vk::ImageLayout::eTransferSrcOptimal,
		VK_QUEUE_FAMILY_IGNORED,
		VK_QUEUE_FAMILY_IGNORED,
		image.GetImage(),
		vk::ImageSubresourceRange(FormatAspectFlags(createInfo.Format), 0, 1, 0, createInfo.ArrayLayers));

	const vk::ImageMemoryBarrier bottom(
		{},
		vk::AccessFlagBits::eTransferWrite,
		vk::ImageLayout::eUndefined,
		vk::ImageLayout::eTransferDstOptimal,
		VK_QUEUE_FAMILY_IGNORED,
		VK_QUEUE_FAMILY_IGNORED,
		image.GetImage(),
		vk::ImageSubresourceRange(
			FormatAspectFlags(createInfo.Format), 1, createInfo.MipLevels - 1, 0, createInfo.ArrayLayers));

	if (needTopLevelBarrier) {
		Barrier(srcStages, vk::PipelineStageFlagBits::eTransfer, {}, {}, {top, bottom});
	} else {
		Barrier(srcStages, vk::PipelineStageFlagBits::eTransfer, {}, {}, {bottom});
	}
}

void CommandBuffer::BufferBarrier(const Buffer& buffer,
                                  vk::PipelineStageFlags srcStages,
                                  vk::AccessFlags srcAccess,
                                  vk::PipelineStageFlags dstStages,
                                  vk::AccessFlags dstAccess) {
	const vk::BufferMemoryBarrier barrier(srcAccess,
	                                      dstAccess,
	                                      VK_QUEUE_FAMILY_IGNORED,
	                                      VK_QUEUE_FAMILY_IGNORED,
	                                      buffer.GetBuffer(),
	                                      0,
	                                      buffer.GetCreateInfo().Size);
	_commandBuffer.pipelineBarrier(srcStages, dstStages, {}, nullptr, barrier, nullptr);
}

void CommandBuffer::BufferBarriers(vk::PipelineStageFlags srcStages,
                                   vk::PipelineStageFlags dstStages,
                                   const std::vector<vk::BufferMemoryBarrier>& bufferBarriers) {
	Barrier(srcStages, dstStages, {}, bufferBarriers, {});
}

void CommandBuffer::FullBarrier() {
	Barrier(vk::PipelineStageFlagBits::eAllCommands,
	        vk::AccessFlagBits::eDepthStencilAttachmentWrite | vk::AccessFlagBits::eShaderWrite |
	          vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eTransferWrite,
	        vk::PipelineStageFlagBits::eAllCommands,
	        vk::AccessFlagBits::eDepthStencilAttachmentRead | vk::AccessFlagBits::eDepthStencilAttachmentWrite |
	          vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite |
	          vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite |
	          vk::AccessFlagBits::eTransferRead | vk::AccessFlagBits::eTransferWrite);
}

void CommandBuffer::ImageBarrier(const Image& image,
                                 vk::ImageLayout oldLayout,
                                 vk::ImageLayout newLayout,
                                 vk::PipelineStageFlags srcStages,
                                 vk::AccessFlags srcAccess,
                                 vk::PipelineStageFlags dstStages,
                                 vk::AccessFlags dstAccess) {
	const vk::ImageMemoryBarrier barrier(srcAccess,
	                                     dstAccess,
	                                     oldLayout,
	                                     newLayout,
	                                     VK_QUEUE_FAMILY_IGNORED,
	                                     VK_QUEUE_FAMILY_IGNORED,
	                                     image.GetImage(),
	                                     vk::ImageSubresourceRange(FormatAspectFlags(image.GetCreateInfo().Format),
	                                                               0,
	                                                               image.GetCreateInfo().MipLevels,
	                                                               0,
	                                                               image.GetCreateInfo().ArrayLayers));
	_commandBuffer.pipelineBarrier(srcStages, dstStages, {}, nullptr, nullptr, barrier);
}

void CommandBuffer::ImageBarriers(vk::PipelineStageFlags srcStages,
                                  vk::PipelineStageFlags dstStages,
                                  const std::vector<vk::ImageMemoryBarrier>& imageBarriers) {
	Barrier(srcStages, dstStages, {}, {}, imageBarriers);
}

void CommandBuffer::PixelBarrier() {
	const vk::MemoryBarrier barrier(vk::AccessFlagBits::eColorAttachmentWrite, vk::AccessFlagBits::eInputAttachmentRead);
	_commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eColorAttachmentOutput,
	                               vk::PipelineStageFlagBits::eFragmentShader,
	                               vk::DependencyFlagBits::eByRegion,
	                               barrier,
	                               nullptr,
	                               nullptr);
}

void CommandBuffer::BlitImage(const Image& dst,
                              const Image& src,
                              const vk::Offset3D& dstOffset,
                              const vk::Offset3D& dstExtent,
                              const vk::Offset3D& srcOffset,
                              const vk::Offset3D& srcExtent,
                              uint32_t dstLevel,
                              uint32_t srcLevel,
                              uint32_t dstBaseLayer,
                              uint32_t srcBaseLayer,
                              uint32_t layerCount,
                              vk::Filter filter) {
	const auto AddOffset = [](const vk::Offset3D& a, const vk::Offset3D& b) -> vk::Offset3D {
		return vk::Offset3D(a.x + b.x, a.y + b.y, a.z + b.z);
	};

	const vk::ImageBlit blit(
		vk::ImageSubresourceLayers(FormatAspectFlags(src.GetCreateInfo().Format), srcLevel, srcBaseLayer, layerCount),
		{srcOffset, AddOffset(srcOffset, srcExtent)},
		vk::ImageSubresourceLayers(FormatAspectFlags(dst.GetCreateInfo().Format), dstLevel, dstBaseLayer, layerCount),
		{dstOffset, AddOffset(dstOffset, dstExtent)});
	_commandBuffer.blitImage(src.GetImage(),
	                         src.GetLayout(vk::ImageLayout::eTransferSrcOptimal),
	                         dst.GetImage(),
	                         dst.GetLayout(vk::ImageLayout::eTransferDstOptimal),
	                         blit,
	                         filter);
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

void CommandBuffer::CopyBufferToImage(const Image& dst,
                                      const Buffer& src,
                                      const std::vector<vk::BufferImageCopy>& blits) {
	_commandBuffer.copyBufferToImage(
		src.GetBuffer(), dst.GetImage(), dst.GetLayout(vk::ImageLayout::eTransferDstOptimal), blits);
}

void CommandBuffer::CopyBufferToImage(const Image& dst,
                                      const Buffer& src,
                                      vk::DeviceSize bufferOffset,
                                      const vk::Offset3D& offset,
                                      const vk::Extent3D& extent,
                                      uint32_t rowLength,
                                      uint32_t sliceHeight,
                                      const vk::ImageSubresourceLayers& subresource) {
	const vk::BufferImageCopy blit(bufferOffset, rowLength, sliceHeight, subresource, offset, extent);
	_commandBuffer.copyBufferToImage(
		src.GetBuffer(), dst.GetImage(), dst.GetLayout(vk::ImageLayout::eTransferDstOptimal), blit);
}

void CommandBuffer::FillBuffer(const Buffer& dst, uint8_t value) {
	FillBuffer(dst, value, 0, dst.GetCreateInfo().Size);
}

void CommandBuffer::FillBuffer(const Buffer& dst, uint8_t value, vk::DeviceSize offset, vk::DeviceSize size) {
	_commandBuffer.fillBuffer(dst.GetBuffer(), offset, size, value);
}

void CommandBuffer::GenerateMipmaps(const Image& image) {
	const auto& createInfo = image.GetCreateInfo();
	const vk::Offset3D origin(0, 0, 0);
	vk::Offset3D size(createInfo.Width, createInfo.Height, createInfo.Depth);

	vk::ImageMemoryBarrier barrier(
		vk::AccessFlagBits::eTransferWrite,
		vk::AccessFlagBits::eTransferRead,
		vk::ImageLayout::eTransferDstOptimal,
		vk::ImageLayout::eTransferSrcOptimal,
		VK_QUEUE_FAMILY_IGNORED,
		VK_QUEUE_FAMILY_IGNORED,
		image.GetImage(),
		vk::ImageSubresourceRange(FormatAspectFlags(createInfo.Format), 0, 1, 0, createInfo.ArrayLayers));

	for (uint32_t i = 1; i < createInfo.MipLevels; ++i) {
		const vk::Offset3D srcSize = size;
		size.x                     = std::max(size.x >> 1, 1);
		size.y                     = std::max(size.y >> 1, 1);
		size.z                     = std::max(size.z >> 1, 1);

		BlitImage(image, image, origin, size, origin, srcSize, i, i - 1, 0, 0, createInfo.ArrayLayers, vk::Filter::eLinear);

		barrier.subresourceRange.baseMipLevel = i;
		Barrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eTransfer, {}, {}, {barrier});
	}
}

void CommandBuffer::BeginCompute() {
	_isCompute = true;
	BeginContext();
}

void CommandBuffer::BeginGraphics() {
	_isCompute = false;
	BeginContext();
}

void CommandBuffer::SetOpaqueState() {
	ClearRenderState();

	auto& state            = _pipelineState.StaticState.State;
	state.FrontFace        = uint32_t(vk::FrontFace::eCounterClockwise);
	state.CullMode         = uint32_t(vk::CullModeFlagBits::eBack);
	state.BlendEnable      = false;
	state.DepthTest        = true;
	state.DepthCompare     = uint32_t(vk::CompareOp::eLessOrEqual);
	state.DepthWrite       = true;
	state.DepthBiasEnable  = false;
	state.PrimitiveRestart = false;
	state.StencilTest      = false;
	state.Topology         = uint32_t(vk::PrimitiveTopology::eTriangleList);
	state.WriteMask        = ~0u;

	_dirty |= CommandBufferDirtyFlagBits::StaticState;
}

void CommandBuffer::BeginRenderPass(const RenderPassInfo& info, vk::SubpassContents contents) {
	_framebuffer                        = &_device.RequestFramebuffer(info);
	_pipelineState.CompatibleRenderPass = &_framebuffer->GetCompatibleRenderPass();
	_actualRenderPass                   = &_device.RequestRenderPass(info, false);
	_pipelineState.SubpassIndex         = 0;

	_framebufferAttachments.fill(nullptr);
	for (uint32_t i = 0; i < info.ColorAttachmentCount; ++i) { _framebufferAttachments[i] = info.ColorAttachments[i]; }
	if (info.DepthStencilAttachment) { _framebufferAttachments[info.ColorAttachmentCount] = info.DepthStencilAttachment; }

	SetViewportScissor(info, _framebuffer);

	uint32_t clearValueCount = 0;
	std::array<vk::ClearValue, MaxColorAttachments + 1> clearValues;
	for (uint32_t i = 0; i < info.ColorAttachmentCount; ++i) {
		if (info.ClearAttachmentMask & (1u << i)) {
			clearValues[i].color = info.ColorClearValues[i];
			clearValueCount      = i + 1;
		}

		if (info.ColorAttachments[i]->GetImage()->IsSwapchainImage()) {
			_swapchainStages |= vk::PipelineStageFlagBits::eColorAttachmentOutput;
		}
	}
	if (info.DepthStencilAttachment && (info.Flags & RenderPassOpFlagBits::ClearDepthStencil)) {
		clearValues[info.ColorAttachmentCount].depthStencil = info.DepthStencilClearValue;
		clearValueCount                                     = info.ColorAttachmentCount + 1;
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

void CommandBuffer::BeginContext() {
	_dirty                                                       = ~0u;
	_dirtySetMask                                                = ~0u;
	_pipelineState.Program                                       = nullptr;
	_pipelineState.PotentialStaticState.SpecConstantMask         = 0;
	_pipelineState.PotentialStaticState.InternalSpecConstantMask = 0;
}

void CommandBuffer::ClearRenderState() {
	memset(&_pipelineState.StaticState.State, 0, sizeof(_pipelineState.StaticState.State));
}

void CommandBuffer::SetViewportScissor(const RenderPassInfo& info, const Framebuffer* framebuffer) {
	vk::Rect2D rect     = info.RenderArea;
	vk::Extent2D extent = framebuffer->GetExtent();

	rect.offset.x      = std::min(int32_t(extent.width), rect.offset.x);
	rect.offset.y      = std::min(int32_t(extent.height), rect.offset.y);
	rect.extent.width  = std::min(extent.width - rect.offset.x, rect.extent.width);
	rect.extent.height = std::min(extent.height - rect.offset.y, rect.extent.height);

	_viewport = vk::Viewport(
		float(rect.offset.x), float(rect.offset.y), float(rect.extent.width), float(rect.extent.height), 0.0f, 1.0f);
	_scissor = rect;
}
}  // namespace Vulkan
}  // namespace Luna
