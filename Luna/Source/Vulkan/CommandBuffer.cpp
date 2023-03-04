#include <Luna/Utility/BitOps.hpp>
#include <Luna/Utility/Log.hpp>
#include <Luna/Vulkan/Buffer.hpp>
#include <Luna/Vulkan/CommandBuffer.hpp>
#include <Luna/Vulkan/DescriptorSet.hpp>
#include <Luna/Vulkan/Device.hpp>
#include <Luna/Vulkan/Image.hpp>
#include <Luna/Vulkan/RenderPass.hpp>
#include <Luna/Vulkan/Shader.hpp>

namespace Luna {
namespace Vulkan {
Hash DeferredPipelineCompile::GetHash(uint32_t& activeVBOs) const {
	Hasher h;

	activeVBOs   = 0;
	auto& layout = Program->GetPipelineLayout()->GetResourceLayout();
	ForEachBit(layout.AttributeMask, [&](uint32_t bit) {
		h(bit);
		activeVBOs |= 1u << Attributes[bit].Binding;
		h(Attributes[bit].Binding);
		h(Attributes[bit].Format);
		h(Attributes[bit].Offset);
	});

	ForEachBit(activeVBOs, [&](uint32_t bit) {
		h(InputRates[bit]);
		h(Strides[bit]);
	});

	h(CompatibleRenderPass->GetHash());
	h(SubpassIndex);
	h(Program->GetHash());
	h.Data(sizeof(StaticState.Words), StaticState.Words);

	if (StaticState.BlendEnable) {
		const auto NeedsBlendConstant = [](vk::BlendFactor factor) {
			return factor == vk::BlendFactor::eConstantColor || factor == vk::BlendFactor::eConstantAlpha;
		};
		const bool b0 = NeedsBlendConstant(vk::BlendFactor(StaticState.SrcColorBlend));
		const bool b1 = NeedsBlendConstant(vk::BlendFactor(StaticState.SrcAlphaBlend));
		const bool b2 = NeedsBlendConstant(vk::BlendFactor(StaticState.DstColorBlend));
		const bool b3 = NeedsBlendConstant(vk::BlendFactor(StaticState.DstAlphaBlend));
		if (b0 || b1 || b2 || b3) {
			h(PotentialStaticState.BlendConstants[0]);
			h(PotentialStaticState.BlendConstants[1]);
			h(PotentialStaticState.BlendConstants[2]);
			h(PotentialStaticState.BlendConstants[3]);
		}
	}

	return h.Get();
}

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

void CommandBuffer::Barrier(const vk::DependencyInfo& dep) {
	if (_device._deviceInfo.EnabledFeatures.Synchronization2.synchronization2) {
		_commandBuffer.pipelineBarrier2(dep);
	} else {
		// TODO
		throw std::runtime_error("TODO: Add Synchronization1 support.");
	}
}

void CommandBuffer::BarrierPrepareGenerateMipmaps(const Image& image,
                                                  vk::ImageLayout baseLevelLayout,
                                                  vk::PipelineStageFlags2 srcStages,
                                                  vk::AccessFlags2 srcAccess,
                                                  bool needTopLevelBarrier) {
	const auto& createInfo = image.GetCreateInfo();

	const vk::ImageMemoryBarrier2 top(
		srcStages,
		srcAccess,
		vk::PipelineStageFlagBits2::eBlit,
		vk::AccessFlagBits2::eTransferRead,
		baseLevelLayout,
		vk::ImageLayout::eTransferSrcOptimal,
		VK_QUEUE_FAMILY_IGNORED,
		VK_QUEUE_FAMILY_IGNORED,
		image.GetImage(),
		vk::ImageSubresourceRange(
			FormatAspectFlags(image.GetCreateInfo().Format), 0, 1, 0, image.GetCreateInfo().ArrayLayers));

	const vk::ImageMemoryBarrier2 bottom(srcStages,
	                                     vk::AccessFlagBits2::eNone,
	                                     vk::PipelineStageFlagBits2::eBlit,
	                                     vk::AccessFlagBits2::eTransferWrite,
	                                     vk::ImageLayout::eUndefined,
	                                     vk::ImageLayout::eTransferDstOptimal,
	                                     VK_QUEUE_FAMILY_IGNORED,
	                                     VK_QUEUE_FAMILY_IGNORED,
	                                     image.GetImage(),
	                                     vk::ImageSubresourceRange(FormatAspectFlags(image.GetCreateInfo().Format),
	                                                               1,
	                                                               image.GetCreateInfo().MipLevels - 1,
	                                                               0,
	                                                               image.GetCreateInfo().ArrayLayers));

	if (needTopLevelBarrier) {
		ImageBarriers({top, bottom});
	} else {
		ImageBarriers({bottom});
	}
}

void CommandBuffer::ImageBarrier(const Image& image,
                                 vk::ImageLayout oldLayout,
                                 vk::ImageLayout newLayout,
                                 vk::PipelineStageFlags2 srcStages,
                                 vk::AccessFlags2 srcAccess,
                                 vk::PipelineStageFlags2 dstStages,
                                 vk::AccessFlags2 dstAccess) {
	const vk::ImageMemoryBarrier2 barrier(srcStages,
	                                      srcAccess,
	                                      dstStages,
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

	ImageBarriers({barrier});
}

void CommandBuffer::ImageBarriers(const std::vector<vk::ImageMemoryBarrier2>& barriers) {
	const vk::DependencyInfo dep({}, nullptr, nullptr, barriers);
	Barrier(dep);
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

void CommandBuffer::ClearColorImage(const Image& image, const vk::ClearColorValue& clear) {
	const vk::ImageSubresourceRange range(FormatAspectFlags(image.GetCreateInfo().Format),
	                                      0,
	                                      image.GetCreateInfo().MipLevels,
	                                      0,
	                                      image.GetCreateInfo().ArrayLayers);

	ClearColorImage(image, clear, {range});
}

void CommandBuffer::ClearColorImage(const Image& image,
                                    const vk::ClearColorValue& clear,
                                    const std::vector<vk::ImageSubresourceRange>& ranges) {
	_commandBuffer.clearColorImage(
		image.GetImage(), image.GetLayout(vk::ImageLayout::eTransferDstOptimal), clear, ranges);

	if (image.IsSwapchainImage()) { _swapchainStages |= vk::PipelineStageFlagBits2::eClear; }
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

	vk::ImageMemoryBarrier2 barrier(
		vk::PipelineStageFlagBits2::eBlit,
		vk::AccessFlagBits2::eTransferWrite,
		vk::PipelineStageFlagBits2::eBlit,
		vk::AccessFlagBits2::eTransferRead,
		vk::ImageLayout::eTransferDstOptimal,
		vk::ImageLayout::eTransferSrcOptimal,
		VK_QUEUE_FAMILY_IGNORED,
		VK_QUEUE_FAMILY_IGNORED,
		image.GetImage(),
		vk::ImageSubresourceRange(
			FormatAspectFlags(image.GetCreateInfo().Format), 0, 1, 0, image.GetCreateInfo().ArrayLayers));

	for (uint32_t i = 1; i < createInfo.MipLevels; ++i) {
		const vk::Offset3D srcSize = size;
		size.x                     = std::max(size.x >> 1, 1);
		size.y                     = std::max(size.y >> 1, 1);
		size.z                     = std::max(size.z >> 1, 1);

		BlitImage(image, image, origin, size, origin, srcSize, i, i - 1, 0, 0, createInfo.ArrayLayers, vk::Filter::eLinear);

		barrier.subresourceRange.baseMipLevel = i;
		ImageBarriers({barrier});
	}
}

void CommandBuffer::ClearRenderState() {
	memset(&_pipelineState.StaticState, 0, sizeof(_pipelineState.StaticState));
	_dirty |= CommandBufferDirtyFlagBits::StaticState;
}

void CommandBuffer::SetOpaqueState() {
	ClearRenderState();
	auto& state            = _pipelineState.StaticState;
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

void CommandBuffer::Draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) {
	if (FlushRenderState(true)) { _commandBuffer.draw(vertexCount, instanceCount, firstVertex, firstInstance); }
}

void CommandBuffer::SetProgram(Program* program) {
	if (_pipelineState.Program == program) { return; }

	_pipelineState.Program    = program;
	_currentPipeline.Pipeline = VK_NULL_HANDLE;

	_dirty |= CommandBufferDirtyFlagBits::Pipeline;
	if (!program) { return; }

	if (!_programLayout) {
		_dirty |= CommandBufferDirtyFlagBits::PushConstants;
		_dirtySets = ~0u;

		_programLayout  = program->GetPipelineLayout();
		_pipelineLayout = _programLayout->GetPipelineLayout();
	} else if (program->GetPipelineLayout()->GetHash() != _programLayout->GetHash()) {
		auto& newLayout = program->GetPipelineLayout()->GetResourceLayout();
		auto& oldLayout = _programLayout->GetResourceLayout();

		if (newLayout.PushConstantLayoutHash != oldLayout.PushConstantLayoutHash) {
			_dirty |= CommandBufferDirtyFlagBits::PushConstants;
			_dirtySets = ~0u;
		} else {
			auto* newPipelineLayout = program->GetPipelineLayout();
			for (uint32_t set = 0; set < MaxDescriptorSets; ++set) {
				if (newPipelineLayout->GetAllocator(set) != _programLayout->GetAllocator(set)) {
					_dirtySets |= ~((1u << set) - 1);
					break;
				}
			}
		}
	}
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
			_swapchainStages |= vk::PipelineStageFlagBits2::eColorAttachmentOutput;
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

void CommandBuffer::BeginCompute() {
	_isCompute = true;
	BeginContext();
}

void CommandBuffer::BeginContext() {
	_dirty                                                       = ~0u;
	_dirtySets                                                   = ~0u;
	_dirtyVBOs                                                   = ~0u;
	_currentPipeline.Pipeline                                    = nullptr;
	_pipelineLayout                                              = nullptr;
	_programLayout                                               = nullptr;
	_pipelineState.Program                                       = nullptr;
	_pipelineState.PotentialStaticState.SpecConstantMask         = 0;
	_pipelineState.PotentialStaticState.InternalSpecConstantMask = 0;
}

void CommandBuffer::BeginGraphics() {
	_isCompute = false;
	BeginContext();
}

void CommandBuffer::BindPipeline(vk::PipelineBindPoint bindPoint, vk::Pipeline pipeline, uint32_t activeDynamicState) {
	_commandBuffer.bindPipeline(bindPoint, pipeline);

	CommandBufferDirtyFlags staticStateClobber =
		CommandBufferDirtyFlags(~activeDynamicState) & CommandBufferDirtyFlagBits::Dynamic;
	_dirty |= staticStateClobber;
}

Pipeline CommandBuffer::BuildGraphicsPipeline(bool synchronous) {
	const auto& rp    = _pipelineState.CompatibleRenderPass;
	const auto& state = _pipelineState.StaticState;

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
	const uint32_t colorAttachmentCount = rp->GetColorAttachmentCount(_pipelineState.SubpassIndex);
	const vk::PipelineColorBlendStateCreateInfo blending(
		{}, VK_FALSE, vk::LogicOp::eCopy, colorAttachmentCount, blendAttachments.data(), {1.0f, 1.0f, 1.0f, 1.0f});
	for (uint32_t i = 0; i < colorAttachmentCount; ++i) {
		auto& att = blendAttachments[i];

		if (_pipelineState.CompatibleRenderPass->GetColorAttachment(_pipelineState.SubpassIndex, i).attachment !=
		      VK_ATTACHMENT_UNUSED &&
		    (_pipelineState.Program->GetPipelineLayout()->GetResourceLayout().RenderTargetMask & (1u << i))) {
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

	vk::PipelineDepthStencilStateCreateInfo depthStencil({},
	                                                     rp->HasDepth(_pipelineState.SubpassIndex) && state.DepthTest,
	                                                     rp->HasDepth(_pipelineState.SubpassIndex) && state.DepthWrite,
	                                                     {},
	                                                     {},
	                                                     rp->HasStencil(_pipelineState.SubpassIndex) && state.StencilTest,
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
	const uint32_t attributeMask = _pipelineState.Program->GetPipelineLayout()->GetResourceLayout().AttributeMask;
	ForEachBit(attributeMask, [&](uint32_t bit) {
		auto& attr    = vertexAttributes[vertexAttributeCount++];
		attr.location = bit;
		attr.binding  = _pipelineState.Attributes[bit].Binding;
		attr.format   = _pipelineState.Attributes[bit].Format;
		attr.offset   = _pipelineState.Attributes[bit].Offset;
	});
	uint32_t vertexBindingCount                                                     = 0;
	std::array<vk::VertexInputBindingDescription, MaxVertexBindings> vertexBindings = {};
	const uint32_t bindingMask                                                      = _activeVBOs;
	ForEachBit(bindingMask, [&](uint32_t bit) {
		auto& bind     = vertexBindings[vertexBindingCount++];
		bind.binding   = bit;
		bind.inputRate = _pipelineState.InputRates[bit];
		bind.stride    = _pipelineState.Strides[bit];
	});
	const vk::PipelineVertexInputStateCreateInfo vertexInput(
		{}, vertexBindingCount, vertexBindings.data(), vertexAttributeCount, vertexAttributes.data());

	const vk::PipelineInputAssemblyStateCreateInfo assembly(
		{}, static_cast<vk::PrimitiveTopology>(state.Topology), state.PrimitiveRestart);

	vk::PipelineMultisampleStateCreateInfo multisample(
		{}, vk::SampleCountFlagBits::e1, VK_FALSE, 0.0f, nullptr, VK_FALSE, VK_FALSE);
	if (rp->GetSampleCount(_pipelineState.SubpassIndex) != vk::SampleCountFlagBits::e1) {
		multisample.alphaToCoverageEnable = state.AlphaToCoverage;
		multisample.alphaToOneEnable      = state.AlphaToOne;
		multisample.sampleShadingEnable   = state.SampleShading;
		multisample.minSampleShading      = 1.0f;
	}

	const vk::PipelineRasterizationStateCreateInfo rasterizer(
		{},
		VK_FALSE,
		VK_FALSE,
		state.Wireframe ? vk::PolygonMode::eLine : vk::PolygonMode::eFill,
		static_cast<vk::CullModeFlagBits>(state.CullMode),
		static_cast<vk::FrontFace>(state.FrontFace),
		state.DepthBiasEnable,
		0.0f,
		0.0f,
		0.0f,
		1.0f);

	const vk::PipelineTessellationStateCreateInfo tessellation({}, 0);

	bool hasTessellation = false;
	std::vector<vk::PipelineShaderStageCreateInfo> stages;
	stages.push_back(
		vk::PipelineShaderStageCreateInfo({},
	                                    vk::ShaderStageFlagBits::eVertex,
	                                    _pipelineState.Program->GetShader(ShaderStage::Vertex)->GetShaderModule(),
	                                    "main",
	                                    nullptr));
	if (_pipelineState.Program->GetShader(ShaderStage::TessellationControl)) {
		hasTessellation = true;
		stages.push_back(vk::PipelineShaderStageCreateInfo(
			{},
			vk::ShaderStageFlagBits::eTessellationControl,
			_pipelineState.Program->GetShader(ShaderStage::TessellationControl)->GetShaderModule(),
			"main",
			nullptr));
	}
	if (_pipelineState.Program->GetShader(ShaderStage::TessellationEvaluation)) {
		hasTessellation = true;
		stages.push_back(vk::PipelineShaderStageCreateInfo(
			{},
			vk::ShaderStageFlagBits::eTessellationEvaluation,
			_pipelineState.Program->GetShader(ShaderStage::TessellationEvaluation)->GetShaderModule(),
			"main",
			nullptr));
	}
	stages.push_back(
		vk::PipelineShaderStageCreateInfo({},
	                                    vk::ShaderStageFlagBits::eFragment,
	                                    _pipelineState.Program->GetShader(ShaderStage::Fragment)->GetShaderModule(),
	                                    "main",
	                                    nullptr));

	const vk::GraphicsPipelineCreateInfo pipelineCI({},
	                                                stages,
	                                                &vertexInput,
	                                                &assembly,
	                                                hasTessellation ? &tessellation : nullptr,
	                                                &viewport,
	                                                &rasterizer,
	                                                &multisample,
	                                                &depthStencil,
	                                                &blending,
	                                                &dynamic,
	                                                _pipelineLayout,
	                                                _pipelineState.CompatibleRenderPass->GetRenderPass(),
	                                                _pipelineState.SubpassIndex,
	                                                VK_NULL_HANDLE,
	                                                0);
	const auto pipelineResult   = _device.GetDevice().createGraphicsPipeline(VK_NULL_HANDLE, pipelineCI);
	const auto returnedPipeline = _pipelineState.Program->AddPipeline(_pipelineState.Hash, pipelineResult.value);
	if (returnedPipeline != pipelineResult.value) { _device.GetDevice().destroyPipeline(pipelineResult.value); }
	Log::Debug("Vulkan", "Pipeline created.");

	return {returnedPipeline, 0};
}

bool CommandBuffer::FlushGraphicsPipeline(bool synchronous) {
	_pipelineState.Hash       = _pipelineState.GetHash(_activeVBOs);
	_currentPipeline.Pipeline = _pipelineState.Program->GetPipeline(_pipelineState.Hash);
	if (!_currentPipeline.Pipeline) { _currentPipeline = BuildGraphicsPipeline(synchronous); }

	return bool(_currentPipeline.Pipeline);
}

bool CommandBuffer::FlushRenderState(bool synchronous) {
	if (!_pipelineState.Program) { return false; }
	if (!_currentPipeline.Pipeline) { _dirty |= CommandBufferDirtyFlagBits::Pipeline; }

	if (_dirty & (CommandBufferDirtyFlagBits::StaticState | CommandBufferDirtyFlagBits::Pipeline |
	              CommandBufferDirtyFlagBits::StaticVertex)) {
		vk::Pipeline oldPipeline = _currentPipeline.Pipeline;
		if (!FlushGraphicsPipeline(synchronous)) { return false; }

		if (oldPipeline != _currentPipeline.Pipeline) {
			BindPipeline(vk::PipelineBindPoint::eGraphics, _currentPipeline.Pipeline, _currentPipeline.DynamicMask);
		}
	}
	_dirty &= ~(CommandBufferDirtyFlagBits::StaticState | CommandBufferDirtyFlagBits::Pipeline |
	            CommandBufferDirtyFlagBits::StaticVertex);

	if (!_currentPipeline.Pipeline) { return false; }

	// Flush descriptor sets.
	// FlushDescriptorSets();

	if (_dirty & CommandBufferDirtyFlagBits::PushConstants) {
		const auto& range = _programLayout->GetResourceLayout().PushConstantRange;
		if (range.stageFlags) {
			// _commandBuffer.pushConstants(_pipelineLayout, range.stageFlags, 0, range.size,
			// _descriptorBinding.PushConstantData);
		}
	}
	_dirty &= ~CommandBufferDirtyFlagBits::PushConstants;

	if (_dirty & CommandBufferDirtyFlagBits::Viewport) { _commandBuffer.setViewport(0, _viewport); }
	_dirty &= ~CommandBufferDirtyFlagBits::Viewport;

	if (_dirty & CommandBufferDirtyFlagBits::Scissor) { _commandBuffer.setScissor(0, _scissor); }
	_dirty &= ~CommandBufferDirtyFlagBits::Scissor;

	if (_pipelineState.StaticState.DepthBiasEnable && _dirty & CommandBufferDirtyFlagBits::DepthBias) {
		_commandBuffer.setDepthBias(_dynamicState.DepthBiasConstant, 0.0f, _dynamicState.DepthBiasSlope);
	}
	_dirty &= ~CommandBufferDirtyFlagBits::DepthBias;

	const uint32_t updateVBOs = _dirtyVBOs & _activeVBOs;
	ForEachBitRange(updateVBOs, [&](uint32_t binding, uint32_t bindingCount) {
		_commandBuffer.bindVertexBuffers(
			binding, bindingCount, &_vertexBindings.Buffers[binding], &_vertexBindings.Offsets[binding]);
	});
	_dirtyVBOs &= ~updateVBOs;

	return true;
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
