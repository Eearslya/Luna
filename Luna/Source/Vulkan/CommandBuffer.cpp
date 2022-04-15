#include <Luna/Core/Log.hpp>
#include <Luna/Utility/BitOps.hpp>
#include <Luna/Vulkan/Buffer.hpp>
#include <Luna/Vulkan/CommandBuffer.hpp>
#include <Luna/Vulkan/Device.hpp>
#include <Luna/Vulkan/Format.hpp>
#include <Luna/Vulkan/Image.hpp>
#include <Luna/Vulkan/RenderPass.hpp>
#include <Luna/Vulkan/Sampler.hpp>
#include <Luna/Vulkan/Shader.hpp>
#include <TracyVulkan.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace Luna {
namespace Vulkan {
Hash PipelineCompileInfo::GetHash() const {
	Hasher h;

	const auto& layout  = Program->GetPipelineLayout()->GetResourceLayout();
	ActiveVertexBuffers = 0;
	ForEachBit(layout.AttributeMask, [&](uint32_t bit) {
		ActiveVertexBuffers |= 1u << VertexAttributes[bit].Binding;
		h(bit);
		h(VertexAttributes[bit].Binding);
		h(VertexAttributes[bit].Format);
		h(VertexAttributes[bit].Offset);
	});
	ForEachBit(ActiveVertexBuffers, [&](uint32_t bit) {
		h(VertexInputRates[bit]);
		h(VertexStrides[bit]);
	});

	h(CompatibleRenderPass->GetHash());
	h(Program->GetHash());
	h.Data(sizeof(StaticState.Data), StaticState.Data);

	return h.Get();
}

void CommandBufferDeleter::operator()(CommandBuffer* buffer) {
	buffer->_device.ReleaseCommandBuffer({}, buffer);
}

CommandBuffer::CommandBuffer(Device& device,
                             vk::CommandBuffer commandBuffer,
                             CommandBufferType type,
                             uint32_t threadIndex)
		: _device(device), _commandBuffer(commandBuffer), _commandBufferType(type), _threadIndex(threadIndex) {
	BeginCompute();
	SetOpaqueState();
	_tracing = _device.GetTracing({}, type);
}

CommandBuffer::~CommandBuffer() noexcept {}

void CommandBuffer::Begin() {
	ZoneScopedN("Vulkan::CommandBuffer::Begin");
	const vk::CommandBufferBeginInfo cmdBI(vk::CommandBufferUsageFlagBits::eOneTimeSubmit, nullptr);
	_commandBuffer.begin(cmdBI);
}

void CommandBuffer::End() {
	if (_zoneDepth != 0) {
		Log::Error("[Vulkan::CommandBuffer] Command Buffer has mismatched BeginZone()/EndZone() calls!");
		for (size_t i = 0; i < _zoneDepth; ++i) { EndZone(); }
	}
	TracyVkCollect(_tracing, _commandBuffer);
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
                            const vk::ArrayProxy<const vk::MemoryBarrier>& barriers,
                            const vk::ArrayProxy<const vk::BufferMemoryBarrier>& buffers,
                            const vk::ArrayProxy<const vk::ImageMemoryBarrier>& images) {
	_commandBuffer.pipelineBarrier(srcStages, dstStages, {}, barriers, buffers, images);
}

void CommandBuffer::BlitImage(Image& dst,
                              Image& src,
                              const vk::Offset3D& dstOffset,
                              const vk::Extent3D& dstExtent,
                              const vk::Offset3D& srcOffset,
                              const vk::Extent3D& srcExtent,
                              uint32_t dstLevel,
                              uint32_t srcLevel,
                              uint32_t dstBaseLayer,
                              uint32_t srcBaseLayer,
                              uint32_t layerCount,
                              vk::Filter filter) {
	const auto AddOffset = [](const vk::Offset3D& a, const vk::Extent3D& b) -> vk::Offset3D {
		return {
			a.x + static_cast<int32_t>(b.width), a.y + static_cast<int32_t>(b.height), a.z + static_cast<int32_t>(b.depth)};
	};

	for (uint32_t i = 0; i < layerCount; ++i) {
		const vk::ImageBlit blit(
			vk::ImageSubresourceLayers(FormatToAspect(src.GetCreateInfo().Format), srcLevel, srcBaseLayer + i, 1),
			{srcOffset, AddOffset(srcOffset, srcExtent)},
			vk::ImageSubresourceLayers(FormatToAspect(dst.GetCreateInfo().Format), dstLevel, dstBaseLayer + i, 1),
			{dstOffset, AddOffset(dstOffset, dstExtent)});
		_commandBuffer.blitImage(src.GetImage(),
		                         src.GetLayout(vk::ImageLayout::eTransferSrcOptimal),
		                         dst.GetImage(),
		                         dst.GetLayout(vk::ImageLayout::eTransferDstOptimal),
		                         blit,
		                         filter);
	}
}

void CommandBuffer::CopyBuffer(Buffer& dst, Buffer& src) {
	CopyBuffer(dst, 0, src, 0, dst.GetCreateInfo().Size);
}

void CommandBuffer::CopyBuffer(
	Buffer& dst, vk::DeviceSize dstOffset, Buffer& src, vk::DeviceSize srcOffset, vk::DeviceSize bytes) {
	const vk::BufferCopy region(srcOffset, dstOffset, bytes);
	_commandBuffer.copyBuffer(src.GetBuffer(), dst.GetBuffer(), region);
}

void CommandBuffer::CopyBufferToImage(Image& dst, Buffer& src, const std::vector<vk::BufferImageCopy>& copies) {
	_commandBuffer.copyBufferToImage(
		src.GetBuffer(), dst.GetImage(), dst.GetLayout(vk::ImageLayout::eTransferDstOptimal), copies);
}

void CommandBuffer::CopyImage(Image& dst,
                              Image& src,
                              const vk::Offset3D& dstOffset,
                              const vk::Offset3D& srcOffset,
                              const vk::Extent3D& extent,
                              const vk::ImageSubresourceLayers& dstSubresource,
                              const vk::ImageSubresourceLayers& srcSubresource) {
	const vk::ImageCopy region(srcSubresource, srcOffset, dstSubresource, dstOffset, extent);
	_commandBuffer.copyImage(src.GetImage(),
	                         src.GetLayout(vk::ImageLayout::eTransferSrcOptimal),
	                         dst.GetImage(),
	                         dst.GetLayout(vk::ImageLayout::eTransferDstOptimal),
	                         region);
}

void CommandBuffer::GenerateMipmaps(Image& image,
                                    vk::ImageLayout baseLayout,
                                    vk::PipelineStageFlags srcStage,
                                    vk::AccessFlags srcAccess,
                                    bool needTopLevelBarrier) {
	const auto& createInfo = image.GetCreateInfo();

	// We start with the top and bottom barriers, which will prepare the mips and finalize them.
	{
		std::array<vk::ImageMemoryBarrier, 2> barriers;
		for (uint32_t i = 0; i < 2; ++i) {
			const bool top = i == 0;
			barriers[i] =
				vk::ImageMemoryBarrier(top ? srcAccess : vk::AccessFlags(),
			                         top ? vk::AccessFlagBits::eTransferRead : vk::AccessFlagBits::eTransferWrite,
			                         top ? baseLayout : vk::ImageLayout::eUndefined,
			                         top ? vk::ImageLayout::eTransferSrcOptimal : vk::ImageLayout::eTransferDstOptimal,
			                         VK_QUEUE_FAMILY_IGNORED,
			                         VK_QUEUE_FAMILY_IGNORED,
			                         image.GetImage(),
			                         vk::ImageSubresourceRange(FormatToAspect(createInfo.Format),
			                                                   top ? 0 : 1,
			                                                   top ? 1 : createInfo.MipLevels - 1,
			                                                   0,
			                                                   createInfo.ArrayLayers));
		}

		if (needTopLevelBarrier) {
			Barrier(srcStage, vk::PipelineStageFlagBits::eTransfer, nullptr, nullptr, barriers);
		} else {
			Barrier(srcStage, vk::PipelineStageFlagBits::eTransfer, nullptr, nullptr, barriers[1]);
		}
	}

	// Now we go down each mip level and blit to generate our mips.
	vk::Extent3D size = createInfo.Extent;
	const vk::Offset3D origin;

	vk::ImageMemoryBarrier barrier(
		vk::AccessFlagBits::eTransferWrite,
		vk::AccessFlagBits::eTransferRead,
		vk::ImageLayout::eTransferDstOptimal,
		vk::ImageLayout::eTransferSrcOptimal,
		VK_QUEUE_FAMILY_IGNORED,
		VK_QUEUE_FAMILY_IGNORED,
		image.GetImage(),
		vk::ImageSubresourceRange(FormatToAspect(createInfo.Format), 0, 1, 0, createInfo.ArrayLayers));
	for (uint32_t i = 1; i < createInfo.MipLevels; ++i) {
		const vk::Extent3D srcSize = size;
		size.width                 = std::max(size.width >> 1u, 1u);
		size.height                = std::max(size.height >> 1u, 1u);
		size.depth                 = std::max(size.depth >> 1u, 1u);

		BlitImage(image, image, origin, size, origin, srcSize, i, i - 1, 0, 0, createInfo.ArrayLayers, vk::Filter::eLinear);

		barrier.subresourceRange.baseMipLevel = i;

		Barrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eTransfer, nullptr, nullptr, barrier);
	}
}

void CommandBuffer::ImageBarrier(Image& image,
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
	                                     vk::ImageSubresourceRange(FormatToAspect(image.GetCreateInfo().Format),
	                                                               0,
	                                                               image.GetCreateInfo().MipLevels,
	                                                               0,
	                                                               image.GetCreateInfo().ArrayLayers));
	_commandBuffer.pipelineBarrier(srcStages, dstStages, {}, nullptr, nullptr, barrier);
}

void CommandBuffer::BeginRenderPass(const RenderPassInfo& info) {
	_framebuffer                              = &_device.RequestFramebuffer({}, info);
	_pipelineCompileInfo.CompatibleRenderPass = &_framebuffer->GetCompatibleRenderPass();
	_pipelineCompileInfo.SubpassIndex         = 0;
	_actualRenderPass                         = &_device.RequestRenderPass(Badge<CommandBuffer>{}, info, false);

	uint32_t attachment = 0;
	for (attachment = 0; attachment < info.ColorAttachmentCount; ++attachment) {
		_framebufferAttachments[attachment] = info.ColorAttachments[attachment];
	}
	if (info.DepthStencilAttachment) { _framebufferAttachments[attachment++] = info.DepthStencilAttachment; }

	uint32_t clearValueCount = 0;
	std::array<vk::ClearValue, MaxColorAttachments + 1> clearValues;
	for (uint32_t i = 0; i < info.ColorAttachmentCount; ++i) {
		if (info.ClearAttachments & (1u << i)) {
			clearValues[i].setColor(info.ClearColors[i]);
			clearValueCount = i + 1;
		}
		if (info.ColorAttachments[i]->GetImage().IsSwapchainImage()) {
			_swapchainStages |= vk::PipelineStageFlagBits::eColorAttachmentOutput;
		}
	}
	if (info.DepthStencilAttachment && (info.DSOps & DepthStencilOpBits::ClearDepthStencil)) {
		clearValues[info.ColorAttachmentCount].setDepthStencil(info.ClearDepthStencil);
		clearValueCount = info.ColorAttachmentCount + 1;
	}

	SetViewportScissor(info);

	const vk::RenderPassBeginInfo rpBI(
		_actualRenderPass->GetRenderPass(), _framebuffer->GetFramebuffer(), _scissor, clearValueCount, clearValues.data());
	_commandBuffer.beginRenderPass(rpBI, vk::SubpassContents::eInline);

	BeginGraphics();
}

void CommandBuffer::NextSubpass() {
	_commandBuffer.nextSubpass(vk::SubpassContents::eInline);

	_pipelineCompileInfo.SubpassIndex++;
}

void CommandBuffer::EndRenderPass() {
	_commandBuffer.endRenderPass();

	_framebuffer                              = nullptr;
	_pipelineCompileInfo.CompatibleRenderPass = nullptr;
	_actualRenderPass                         = nullptr;

	TracyVkCollect(_tracing, _commandBuffer);
}

/* ==========
 * Static State
 * ========== */

#define SetStaticState(field, value)                                              \
	do {                                                                            \
		if (_pipelineCompileInfo.StaticState.field != static_cast<unsigned>(value)) { \
			_pipelineCompileInfo.StaticState.field = static_cast<unsigned>(value);      \
			_dirty |= CommandBufferDirtyFlagBits::StaticState;                          \
		}                                                                             \
	} while (0)

void CommandBuffer::ClearRenderState() {
	memset(&_pipelineCompileInfo.StaticState, 0, sizeof(_pipelineCompileInfo.StaticState));
	_dirty |= CommandBufferDirtyFlagBits::StaticState;
}

void CommandBuffer::SetOpaqueState() {
	ClearRenderState();
	auto& state            = _pipelineCompileInfo.StaticState;
	state.FrontFace        = static_cast<unsigned>(vk::FrontFace::eCounterClockwise);
	state.CullMode         = static_cast<unsigned>(vk::CullModeFlagBits::eBack);
	state.BlendEnable      = false;
	state.DepthTest        = true;
	state.DepthCompare     = static_cast<unsigned>(vk::CompareOp::eLessOrEqual);
	state.DepthWrite       = true;
	state.DepthBiasEnable  = false;
	state.PrimitiveRestart = false;
	state.StencilTest      = false;
	state.Topology         = static_cast<unsigned>(vk::PrimitiveTopology::eTriangleList);
	state.WriteMask        = ~0u;
	_dirty |= CommandBufferDirtyFlagBits::StaticState;
}

void CommandBuffer::SetTransparentSpriteState() {
	ClearRenderState();
	auto& state            = _pipelineCompileInfo.StaticState;
	state.FrontFace        = static_cast<unsigned>(vk::FrontFace::eCounterClockwise);
	state.CullMode         = static_cast<unsigned>(vk::CullModeFlagBits::eNone);
	state.BlendEnable      = true;
	state.DepthTest        = true;
	state.DepthCompare     = static_cast<unsigned>(vk::CompareOp::eLess);
	state.DepthWrite       = false;
	state.DepthBiasEnable  = false;
	state.PrimitiveRestart = false;
	state.StencilTest      = false;
	state.Topology         = static_cast<unsigned>(vk::PrimitiveTopology::eTriangleList);
	state.WriteMask        = ~0u;
	state.SrcColorBlend    = static_cast<unsigned>(vk::BlendFactor::eSrcAlpha);
	state.DstColorBlend    = static_cast<unsigned>(vk::BlendFactor::eOneMinusSrcAlpha);
	state.ColorBlendOp     = static_cast<unsigned>(vk::BlendOp::eAdd);
	state.SrcAlphaBlend    = static_cast<unsigned>(vk::BlendFactor::eZero);
	state.DstAlphaBlend    = static_cast<unsigned>(vk::BlendFactor::eOneMinusSrcAlpha);
	state.AlphaBlendOp     = static_cast<unsigned>(vk::BlendOp::eAdd);
	_dirty |= CommandBufferDirtyFlagBits::StaticState;
}

void CommandBuffer::SetCullMode(vk::CullModeFlagBits mode) {
	SetStaticState(CullMode, mode);
}

void CommandBuffer::SetDepthClamp(bool clamp) {
	// Require the device feature to be enabled.
	clamp = clamp & _device.GetGPUInfo().EnabledFeatures.Features.depthClamp;
	SetStaticState(DepthClamp, clamp);
}

void CommandBuffer::SetDepthCompareOp(vk::CompareOp op) {
	SetStaticState(DepthCompare, op);
}

void CommandBuffer::SetFrontFace(vk::FrontFace front) {
	SetStaticState(FrontFace, front);
}

void CommandBuffer::SetScissor(const vk::Rect2D& scissor) {
	_scissor = scissor;
	_dirty |= CommandBufferDirtyFlagBits::Scissor;
}

#undef SetStaticState

void CommandBuffer::Draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) {
	if (FlushRenderState(true)) {
		_commandBuffer.draw(vertexCount, instanceCount, firstVertex, firstInstance);
		_device.AddDrawCall({});
	}
}

void CommandBuffer::DrawIndexed(
	uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance) {
	if (FlushRenderState(true)) {
		_commandBuffer.drawIndexed(indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
		_device.AddDrawCall({});
	}
}

void CommandBuffer::PushConstants(const void* data, vk::DeviceSize offset, vk::DeviceSize range) {
	assert(offset + range <= MaxPushConstantSize);
	memcpy(_descriptorBinding.PushConstantData + offset, data, range);
	_dirty |= CommandBufferDirtyFlagBits::PushConstants;
}

void CommandBuffer::SetIndexBuffer(const Buffer& buffer, vk::DeviceSize offset, vk::IndexType indexType) {
	if (_indexBuffer.Buffer == buffer.GetBuffer() && _indexBuffer.Offset == offset &&
	    _indexBuffer.IndexType == indexType) {
		return;
	}

	_indexBuffer.Buffer    = buffer.GetBuffer();
	_indexBuffer.Offset    = offset;
	_indexBuffer.IndexType = indexType;
	_commandBuffer.bindIndexBuffer(_indexBuffer.Buffer, _indexBuffer.Offset, _indexBuffer.IndexType);
}

void CommandBuffer::SetInputAttachments(uint32_t set, uint32_t firstBinding) {
	const uint32_t inputCount = _actualRenderPass->GetInputAttachmentCount(_pipelineCompileInfo.SubpassIndex);
	for (uint32_t i = 0; i < inputCount; ++i) {
		const auto& ref = _actualRenderPass->GetInputAttachment(_pipelineCompileInfo.SubpassIndex, i);
		if (ref.attachment == VK_ATTACHMENT_UNUSED) { continue; }

		const ImageView* view = _framebufferAttachments[ref.attachment];
		if (view->GetCookie() == _descriptorBinding.Sets[set].Cookies[firstBinding + i] &&
		    _descriptorBinding.Sets[set].Bindings[firstBinding + i].Image.Float.imageLayout == ref.layout) {
			continue;
		}

		auto& binding                                          = _descriptorBinding.Sets[set].Bindings[firstBinding + i];
		binding.Image.Float.imageLayout                        = ref.layout;
		binding.Image.Integer.imageLayout                      = ref.layout;
		binding.Image.Float.imageView                          = view->GetFloatView();
		binding.Image.Integer.imageView                        = view->GetIntegerView();
		_descriptorBinding.Sets[set].Cookies[firstBinding + i] = view->GetCookie();
		_dirtyDescriptorSets |= 1u << set;
	}
}

void CommandBuffer::SetProgram(const Program* program) {
	if (_pipelineCompileInfo.Program == program) { return; }

	_pipelineCompileInfo.Program = program;
	_pipeline                    = VK_NULL_HANDLE;

	_dirty |= CommandBufferDirtyFlagBits::Pipeline | CommandBufferDirtyFlagBits::DynamicState;
	if (!program) { return; }

	if (!_pipelineLayout) {
		_dirty |= CommandBufferDirtyFlagBits::PushConstants;
		_dirtyDescriptorSets = ~0u;

	} else if (program->GetPipelineLayout()->GetHash() != _programLayout->GetHash()) {
		auto& newLayout = program->GetPipelineLayout()->GetResourceLayout();
		auto& oldLayout = _programLayout->GetResourceLayout();

		if (newLayout.PushConstantLayoutHash != oldLayout.PushConstantLayoutHash) {
			_dirty |= CommandBufferDirtyFlagBits::PushConstants;
			_dirtyDescriptorSets = ~0u;
		} else {
			auto* newPipelineLayout = program->GetPipelineLayout();
			for (uint32_t set = 0; set < MaxDescriptorSets; ++set) {}
		}
	}

	_programLayout  = _pipelineCompileInfo.Program->GetPipelineLayout();
	_pipelineLayout = _programLayout->GetPipelineLayout();
}

void CommandBuffer::SetSampler(uint32_t set, uint32_t binding, const Sampler* sampler) {
	const auto cookie = sampler->GetCookie();
	if (cookie == _descriptorBinding.Sets[set].SecondaryCookies[binding]) { return; }

	auto& bind                 = _descriptorBinding.Sets[set].Bindings[binding];
	bind.Image.Float.sampler   = sampler->GetSampler();
	bind.Image.Integer.sampler = sampler->GetSampler();
	_dirtyDescriptorSets |= 1u << set;
	_descriptorBinding.Sets[set].SecondaryCookies[binding] = cookie;
}

void CommandBuffer::SetTexture(uint32_t set, uint32_t binding, const ImageView& view) {
	const auto layout = view.GetImage().GetLayout(vk::ImageLayout::eShaderReadOnlyOptimal);
	const auto cookie = view.GetCookie();
	if (_descriptorBinding.Sets[set].Cookies[binding] == cookie &&
	    _descriptorBinding.Sets[set].Bindings[binding].Image.Float.imageLayout == layout) {
		return;
	}

	auto& bind                                    = _descriptorBinding.Sets[set].Bindings[binding];
	bind.Image.Float.imageLayout                  = layout;
	bind.Image.Float.imageView                    = view.GetFloatView();
	bind.Image.Integer.imageLayout                = layout;
	bind.Image.Integer.imageView                  = view.GetIntegerView();
	_descriptorBinding.Sets[set].Cookies[binding] = cookie;
	_dirtyDescriptorSets |= 1u << set;
}

void CommandBuffer::SetTexture(uint32_t set, uint32_t binding, const ImageView& view, const Sampler* sampler) {
	SetTexture(set, binding, view);
	SetSampler(set, binding, sampler);
}

void CommandBuffer::SetTexture(uint32_t set, uint32_t binding, const ImageView& view, StockSampler stockSampler) {
	const auto sampler = _device.RequestSampler(stockSampler);
	SetTexture(set, binding, view, sampler);
}

void CommandBuffer::SetUniformBuffer(
	uint32_t set, uint32_t binding, const Buffer& buffer, vk::DeviceSize offset, vk::DeviceSize range) {
	if (range == 0) { range = buffer.GetCreateInfo().Size; }

	auto& bind = _descriptorBinding.Sets[set].Bindings[binding];

	if (buffer.GetCookie() == _descriptorBinding.Sets[set].Cookies[binding] && bind.Buffer.range == range) { return; }

	bind.Buffer                                            = vk::DescriptorBufferInfo(buffer.GetBuffer(), offset, range);
	_descriptorBinding.Sets[set].Cookies[binding]          = buffer.GetCookie();
	_descriptorBinding.Sets[set].SecondaryCookies[binding] = 0;
	_dirtyDescriptorSets |= 1u << set;
}

void CommandBuffer::SetVertexAttribute(uint32_t attribute, uint32_t binding, vk::Format format, vk::DeviceSize offset) {
	auto& attr = _pipelineCompileInfo.VertexAttributes[attribute];

	if (attr.Binding != binding || attr.Format != format || attr.Offset != offset) {
		_dirty |= CommandBufferDirtyFlagBits::StaticVertex;
	}

	attr.Binding = binding;
	attr.Format  = format;
	attr.Offset  = offset;
}

void CommandBuffer::SetVertexBinding(
	uint32_t binding, const Buffer& buffer, vk::DeviceSize offset, vk::DeviceSize stride, vk::VertexInputRate inputRate) {
	vk::Buffer vkBuffer = buffer.GetBuffer();

	if (_vertexBindings.Buffers[binding] != vkBuffer || _vertexBindings.Offsets[binding] != offset) {
		_dirtyVertexBuffers |= 1u << binding;
	}
	if (_pipelineCompileInfo.VertexStrides[binding] != stride ||
	    _pipelineCompileInfo.VertexInputRates[binding] != inputRate) {
		_dirty |= CommandBufferDirtyFlagBits::StaticVertex;
	}

	_vertexBindings.Buffers[binding]               = vkBuffer;
	_vertexBindings.Offsets[binding]               = offset;
	_pipelineCompileInfo.VertexInputRates[binding] = inputRate;
	_pipelineCompileInfo.VertexStrides[binding]    = stride;
}

void CommandBuffer::BeginContext() {
	_dirty                       = ~0u;
	_dirtyDescriptorSets         = ~0u;
	_dirtyVertexBuffers          = ~0u;
	_pipeline                    = VK_NULL_HANDLE;
	_pipelineLayout              = VK_NULL_HANDLE;
	_programLayout               = nullptr;
	_pipelineCompileInfo.Program = nullptr;
	for (auto& set : _descriptorBinding.Sets) {
		std::fill(set.Cookies.begin(), set.Cookies.end(), 0);
		std::fill(set.SecondaryCookies.begin(), set.SecondaryCookies.end(), 0);
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

vk::Pipeline CommandBuffer::BuildGraphicsPipeline(bool synchronous) {
	const auto& rp    = _pipelineCompileInfo.CompatibleRenderPass;
	const auto& state = _pipelineCompileInfo.StaticState;

	const vk::PipelineViewportStateCreateInfo viewport({}, 1, nullptr, 1, nullptr);

	std::vector<vk::DynamicState> dynamicStates{vk::DynamicState::eScissor, vk::DynamicState::eViewport};
	if (state.DepthBiasEnable) { dynamicStates.push_back(vk::DynamicState::eDepthBias); }
	if (state.StencilTest) {
		dynamicStates.push_back(vk::DynamicState::eStencilCompareMask);
		dynamicStates.push_back(vk::DynamicState::eStencilReference);
		dynamicStates.push_back(vk::DynamicState::eStencilWriteMask);
	}
	const vk::PipelineDynamicStateCreateInfo dynamic({}, dynamicStates);

	std::array<vk::PipelineColorBlendAttachmentState, MaxColorAttachments> blendAttachments = {};
	const uint32_t colorAttachmentCount = rp->GetColorAttachmentCount(_pipelineCompileInfo.SubpassIndex);
	const vk::PipelineColorBlendStateCreateInfo blending(
		{}, VK_FALSE, vk::LogicOp::eCopy, colorAttachmentCount, blendAttachments.data(), {1.0f, 1.0f, 1.0f, 1.0f});
	for (uint32_t i = 0; i < colorAttachmentCount; ++i) {
		auto& att = blendAttachments[i];

		if (_pipelineCompileInfo.CompatibleRenderPass->GetColorAttachment(_pipelineCompileInfo.SubpassIndex, i)
		        .attachment != VK_ATTACHMENT_UNUSED &&
		    (_pipelineCompileInfo.Program->GetPipelineLayout()->GetResourceLayout().RenderTargetMask & (1u << i))) {
			att                = vk::PipelineColorBlendAttachmentState(state.BlendEnable,
                                                  vk::BlendFactor::eOne,
                                                  vk::BlendFactor::eZero,
                                                  vk::BlendOp::eAdd,
                                                  vk::BlendFactor::eOne,
                                                  vk::BlendFactor::eZero,
                                                  vk::BlendOp::eAdd,
                                                  vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                                                    vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA);
			att.colorWriteMask = static_cast<vk::ColorComponentFlags>((state.WriteMask >> (4 * i)) & 0x0f);
			if (state.BlendEnable) {
				att.srcColorBlendFactor = static_cast<vk::BlendFactor>(state.SrcColorBlend);
				att.dstColorBlendFactor = static_cast<vk::BlendFactor>(state.DstColorBlend);
				att.srcAlphaBlendFactor = static_cast<vk::BlendFactor>(state.SrcAlphaBlend);
				att.dstAlphaBlendFactor = static_cast<vk::BlendFactor>(state.DstAlphaBlend);
				att.colorBlendOp        = static_cast<vk::BlendOp>(state.ColorBlendOp);
				att.alphaBlendOp        = static_cast<vk::BlendOp>(state.AlphaBlendOp);
			}
		}
	}

	vk::PipelineDepthStencilStateCreateInfo depthStencil(
		{},
		rp->HasDepth(_pipelineCompileInfo.SubpassIndex) && state.DepthTest,
		rp->HasDepth(_pipelineCompileInfo.SubpassIndex) && state.DepthWrite,
		{},
		{},
		rp->HasStencil(_pipelineCompileInfo.SubpassIndex) && state.StencilTest,
		{},
		{},
		0.0f,
		0.0f);
	if (depthStencil.depthTestEnable) { depthStencil.depthCompareOp = static_cast<vk::CompareOp>(state.DepthCompare); }
	if (depthStencil.stencilTestEnable) {
		depthStencil.front.compareOp   = static_cast<vk::CompareOp>(state.StencilFrontCompareOp);
		depthStencil.front.passOp      = static_cast<vk::StencilOp>(state.StencilFrontPass);
		depthStencil.front.failOp      = static_cast<vk::StencilOp>(state.StencilFrontFail);
		depthStencil.front.depthFailOp = static_cast<vk::StencilOp>(state.StencilFrontDepthFail);
		depthStencil.back.compareOp    = static_cast<vk::CompareOp>(state.StencilBackCompareOp);
		depthStencil.back.passOp       = static_cast<vk::StencilOp>(state.StencilBackPass);
		depthStencil.back.failOp       = static_cast<vk::StencilOp>(state.StencilBackFail);
		depthStencil.back.depthFailOp  = static_cast<vk::StencilOp>(state.StencilBackDepthFail);
	}

	uint32_t vertexAttributeCount                                                         = 0;
	std::array<vk::VertexInputAttributeDescription, MaxVertexAttributes> vertexAttributes = {};
	const uint32_t attributeMask = _pipelineCompileInfo.Program->GetPipelineLayout()->GetResourceLayout().AttributeMask;
	ForEachBit(attributeMask, [&](uint32_t bit) {
		auto& attr    = vertexAttributes[vertexAttributeCount++];
		attr.location = bit;
		attr.binding  = _pipelineCompileInfo.VertexAttributes[bit].Binding;
		attr.format   = _pipelineCompileInfo.VertexAttributes[bit].Format;
		attr.offset   = _pipelineCompileInfo.VertexAttributes[bit].Offset;
	});
	uint32_t vertexBindingCount                                                    = 0;
	std::array<vk::VertexInputBindingDescription, MaxVertexBuffers> vertexBindings = {};
	const uint32_t bindingMask = _pipelineCompileInfo.ActiveVertexBuffers;
	ForEachBit(bindingMask, [&](uint32_t bit) {
		auto& bind     = vertexBindings[vertexBindingCount++];
		bind.binding   = bit;
		bind.inputRate = _pipelineCompileInfo.VertexInputRates[bit];
		bind.stride    = _pipelineCompileInfo.VertexStrides[bit];
	});
	const vk::PipelineVertexInputStateCreateInfo vertexInput(
		{}, vertexBindingCount, vertexBindings.data(), vertexAttributeCount, vertexAttributes.data());

	const vk::PipelineInputAssemblyStateCreateInfo assembly(
		{}, static_cast<vk::PrimitiveTopology>(state.Topology), state.PrimitiveRestart);

	vk::PipelineMultisampleStateCreateInfo multisample(
		{}, vk::SampleCountFlagBits::e1, VK_FALSE, 0.0f, nullptr, VK_FALSE, VK_FALSE);
	if (rp->GetSampleCount(_pipelineCompileInfo.SubpassIndex) != vk::SampleCountFlagBits::e1) {
		multisample.alphaToCoverageEnable = state.AlphaToCoverage;
		multisample.alphaToOneEnable      = state.AlphaToOne;
		multisample.sampleShadingEnable   = state.SampleShading;
		multisample.minSampleShading      = 1.0f;
	}

	const vk::PipelineRasterizationStateCreateInfo rasterizer(
		{},
		state.DepthClamp,
		VK_FALSE,
		state.Wireframe ? vk::PolygonMode::eLine : vk::PolygonMode::eFill,
		static_cast<vk::CullModeFlagBits>(state.CullMode),
		static_cast<vk::FrontFace>(state.FrontFace),
		state.DepthBiasEnable,
		0.0f,
		0.0f,
		0.0f,
		1.0f);

	std::vector<vk::PipelineShaderStageCreateInfo> stages;
	stages.push_back(
		vk::PipelineShaderStageCreateInfo({},
	                                    vk::ShaderStageFlagBits::eVertex,
	                                    _pipelineCompileInfo.Program->GetShader(ShaderStage::Vertex)->GetShaderModule(),
	                                    "main",
	                                    nullptr));
	stages.push_back(
		vk::PipelineShaderStageCreateInfo({},
	                                    vk::ShaderStageFlagBits::eFragment,
	                                    _pipelineCompileInfo.Program->GetShader(ShaderStage::Fragment)->GetShaderModule(),
	                                    "main",
	                                    nullptr));

	const vk::GraphicsPipelineCreateInfo pipelineCI({},
	                                                stages,
	                                                &vertexInput,
	                                                &assembly,
	                                                nullptr,
	                                                &viewport,
	                                                &rasterizer,
	                                                &multisample,
	                                                &depthStencil,
	                                                &blending,
	                                                &dynamic,
	                                                _pipelineLayout,
	                                                _pipelineCompileInfo.CompatibleRenderPass->GetRenderPass(),
	                                                _pipelineCompileInfo.SubpassIndex,
	                                                VK_NULL_HANDLE,
	                                                0);
	Log::Trace("[Vulkan::CommandBuffer] Creating new Pipeline.");
	const auto pipelineResult = _device.GetDevice().createGraphicsPipeline(VK_NULL_HANDLE, pipelineCI);
	const auto returnedPipeline =
		_pipelineCompileInfo.Program->AddPipeline(_pipelineCompileInfo.CachedHash, pipelineResult.value);
	if (returnedPipeline != pipelineResult.value) { _device.GetDevice().destroyPipeline(pipelineResult.value); }

	return returnedPipeline;
}

bool CommandBuffer::FlushGraphicsPipeline(bool synchronous) {
	ZoneScopedN("Vulkan::CommandBuffer::FlushGraphicsPipeline()");

	_pipelineCompileInfo.CachedHash = _pipelineCompileInfo.GetHash();
	_pipeline                       = _pipelineCompileInfo.Program->GetPipeline(_pipelineCompileInfo.CachedHash);
	if (!_pipeline) { _pipeline = BuildGraphicsPipeline(synchronous); }
	return bool(_pipeline);
}

bool CommandBuffer::FlushRenderState(bool synchronous) {
	ZoneScopedN("Vulkan::CommandBuffer::FlushRenderState()");

	if (!_pipelineCompileInfo.Program) { return false; }
	if (!_pipeline) { _dirty |= CommandBufferDirtyFlagBits::Pipeline; }

	if (_dirty & (CommandBufferDirtyFlagBits::StaticState | CommandBufferDirtyFlagBits::Pipeline |
	              CommandBufferDirtyFlagBits::StaticVertex)) {
		ZoneScopedN("Pipeline");

		vk::Pipeline oldPipeline = _pipeline;
		if (!FlushGraphicsPipeline(synchronous)) { return false; }

		if (oldPipeline != _pipeline) {
			_commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, _pipeline);
			_dirty |= CommandBufferDirtyFlagBits::DynamicState;
		}
	}
	_dirty &= ~(CommandBufferDirtyFlagBits::StaticState | CommandBufferDirtyFlagBits::Pipeline |
	            CommandBufferDirtyFlagBits::StaticVertex);

	if (!_pipeline) { return false; }

	// Flush descriptor sets.
	{
		ZoneScopedN("Descriptor Sets");

		const auto& layout = _programLayout->GetResourceLayout();

		uint32_t setUpdate = layout.DescriptorSetMask & _dirtyDescriptorSets;
		ForEachBit(setUpdate, [&](uint32_t bit) {
			const auto& setLayout = layout.SetLayouts[bit];
			Hasher h;
			h(setLayout.FloatMask);

			ForEachBit(setLayout.InputAttachmentMask, [&](uint32_t binding) {
				const auto arraySize = setLayout.ArraySizes[binding];
				for (uint32_t i = 0; i < arraySize; ++i) {
					h(_descriptorBinding.Sets[bit].Cookies[binding + i]);
					h(_descriptorBinding.Sets[bit].Bindings[binding + i].Image.Float.imageLayout);
				}
			});
			ForEachBit(setLayout.UniformBufferMask, [&](uint32_t binding) {
				const auto arraySize = setLayout.ArraySizes[binding];
				for (uint32_t i = 0; i < arraySize; ++i) {
					h(_descriptorBinding.Sets[bit].Cookies[binding + i]);
					h(_descriptorBinding.Sets[bit].Bindings[binding + i].Buffer.range);
				}
			});
			ForEachBit(setLayout.SampledImageMask, [&](uint32_t binding) {
				const auto arraySize = setLayout.ArraySizes[binding];
				for (uint32_t i = 0; i < arraySize; ++i) {
					h(_descriptorBinding.Sets[bit].Cookies[binding + i]);
					h(_descriptorBinding.Sets[bit].SecondaryCookies[binding + i]);
					h(_descriptorBinding.Sets[bit].Bindings[binding + i].Image.Float.imageLayout);
				}
			});

			const auto hash = h.Get();
			auto allocated  = _programLayout->GetAllocator(bit)->Find(_threadIndex, hash);

			// If we didn't get an existing set, we need to write it.
			if (!allocated.second) {
				ZoneScopedN("Descriptor Write");

				std::vector<vk::WriteDescriptorSet> writes;

				ForEachBit(setLayout.InputAttachmentMask, [&](uint32_t binding) {
					const auto arraySize = setLayout.ArraySizes[binding];
					for (uint32_t i = 0; i < arraySize; ++i) {
						writes.push_back(
							vk::WriteDescriptorSet(allocated.first,
						                         binding,
						                         i,
						                         1,
						                         vk::DescriptorType::eInputAttachment,
						                         (setLayout.FloatMask & (1u << binding)
						                            ? &_descriptorBinding.Sets[bit].Bindings[binding + i].Image.Float
						                            : &_descriptorBinding.Sets[bit].Bindings[binding + i].Image.Integer),
						                         nullptr,
						                         nullptr));
					}
				});

				ForEachBit(setLayout.UniformBufferMask, [&](uint32_t binding) {
					const auto arraySize = setLayout.ArraySizes[binding];
					for (uint32_t i = 0; i < arraySize; ++i) {
						writes.push_back(vk::WriteDescriptorSet(allocated.first,
						                                        binding,
						                                        i,
						                                        1,
						                                        vk::DescriptorType::eUniformBuffer,
						                                        nullptr,
						                                        &_descriptorBinding.Sets[bit].Bindings[binding + i].Buffer,
						                                        nullptr));
					}
				});

				ForEachBit(setLayout.SampledImageMask, [&](uint32_t binding) {
					const auto arraySize = setLayout.ArraySizes[binding];
					for (uint32_t i = 0; i < arraySize; ++i) {
						writes.push_back(
							vk::WriteDescriptorSet(allocated.first,
						                         binding,
						                         i,
						                         1,
						                         vk::DescriptorType::eCombinedImageSampler,
						                         (setLayout.FloatMask & (1u << binding)
						                            ? &_descriptorBinding.Sets[bit].Bindings[binding + i].Image.Float
						                            : &_descriptorBinding.Sets[bit].Bindings[binding + i].Image.Integer),
						                         nullptr,
						                         nullptr));
					}
				});

				_device.GetDevice().updateDescriptorSets(writes, {});
			}

			_commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, _pipelineLayout, bit, allocated.first, {});
		});
		_dirtyDescriptorSets &= ~setUpdate;
	}

	if (_dirty & CommandBufferDirtyFlagBits::PushConstants) {
		ZoneScopedN("Push Constants");

		const auto& range = _programLayout->GetResourceLayout().PushConstantRange;
		if (range.stageFlags) {
			_commandBuffer.pushConstants(
				_pipelineLayout, range.stageFlags, 0, range.size, _descriptorBinding.PushConstantData);
		}
	}

	if (_dirty & CommandBufferDirtyFlagBits::Viewport) { _commandBuffer.setViewport(0, _viewport); }
	_dirty &= ~CommandBufferDirtyFlagBits::Viewport;

	if (_dirty & CommandBufferDirtyFlagBits::Scissor) { _commandBuffer.setScissor(0, _scissor); }
	_dirty &= ~CommandBufferDirtyFlagBits::Scissor;

	{
		ZoneScopedN("Vertex Buffers");

		const uint32_t updateVBOs = _dirtyVertexBuffers & _pipelineCompileInfo.ActiveVertexBuffers;
		ForEachBitRange(updateVBOs, [&](uint32_t binding, uint32_t bindingCount) {
			_commandBuffer.bindVertexBuffers(
				binding, bindingCount, &_vertexBindings.Buffers[binding], &_vertexBindings.Offsets[binding]);
		});
		_dirtyVertexBuffers &= ~updateVBOs;
	}

	return true;
}

void CommandBuffer::SetViewportScissor(const RenderPassInfo& rpInfo) {
	const auto& fbExtent = _framebuffer->GetExtent();

	_scissor               = rpInfo.RenderArea;
	_scissor.offset.x      = std::min(fbExtent.width, static_cast<uint32_t>(_scissor.offset.x));
	_scissor.offset.y      = std::min(fbExtent.height, static_cast<uint32_t>(_scissor.offset.y));
	_scissor.extent.width  = std::min(fbExtent.width - _scissor.offset.x, _scissor.extent.width);
	_scissor.extent.height = std::min(fbExtent.height - _scissor.offset.y, _scissor.extent.height);

	_viewport = vk::Viewport{static_cast<float>(_scissor.offset.x),
	                         static_cast<float>(_scissor.offset.y),
	                         static_cast<float>(_scissor.extent.width),
	                         static_cast<float>(_scissor.extent.height),
	                         0.0f,
	                         1.0f};
}

void CommandBuffer::BeginZone(const std::string& name, const glm::vec3& color) {
	if (_device.GetExtensionInfo().DebugUtils) {
		const vk::DebugUtilsLabelEXT label(name.c_str(), {color.r, color.g, color.b, 1.0f});
		_commandBuffer.beginDebugUtilsLabelEXT(label);
		_zoneDepth++;
	}
}

void CommandBuffer::EndZone() {
	if (_device.GetExtensionInfo().DebugUtils) {
		_commandBuffer.endDebugUtilsLabelEXT();
		_zoneDepth--;
	}
}

void CommandBuffer::Mark(const std::string& name, const glm::vec3& color) {
	if (_device.GetExtensionInfo().DebugUtils) {
		const vk::DebugUtilsLabelEXT label(name.c_str(), {color.r, color.g, color.b, 1.0f});
		_commandBuffer.insertDebugUtilsLabelEXT(label);
	}
}
}  // namespace Vulkan
}  // namespace Luna
