#include <Luna/Core/Threading.hpp>
#include <Luna/Utility/BitOps.hpp>
#include <Luna/Utility/SpinLock.hpp>
#include <Luna/Vulkan/Buffer.hpp>
#include <Luna/Vulkan/CommandBuffer.hpp>
#include <Luna/Vulkan/DescriptorSet.hpp>
#include <Luna/Vulkan/Device.hpp>
#include <Luna/Vulkan/Image.hpp>
#include <Luna/Vulkan/Sampler.hpp>
#include <Luna/Vulkan/Shader.hpp>

namespace Luna {
namespace Vulkan {
static uint32_t CombinedSpecConstantMask(const DeferredPipelineCompile& pipelineState) {
	return pipelineState.PotentialStaticState.SpecConstantMask |
	       (pipelineState.PotentialStaticState.InternalSpecConstantMask << MaxUserSpecConstants);
}

Hash DeferredPipelineCompile::GetComputeHash() const {
	Hasher h;
	h(Program->GetHash());
	h(PipelineLayout->GetHash());

	auto& layout                  = PipelineLayout->GetResourceLayout();
	uint32_t combinedSpecConstant = layout.CombinedSpecConstantMask;
	combinedSpecConstant &= CombinedSpecConstantMask(*this);
	h(combinedSpecConstant);
	ForEachBit(combinedSpecConstant, [&](uint32_t bit) { h(PotentialStaticState.SpecConstants[bit]); });

	if (StaticState.SubgroupControlSize) {
		h(int32_t(1));
		h(StaticState.SubgroupMinimumSizeLog2);
		h(StaticState.SubgroupMaximumSizeLog2);
		h(StaticState.SubgroupFullGroup);
		h(SubgroupSizeTag);
	} else {
		h(int32_t(0));
	}

	CachedHash = h.Get();

	return CachedHash;
}

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
	BeginCompute();
	SetOpaqueState();
	memset(&_pipelineState.StaticState, 0, sizeof(_pipelineState.StaticState));
	memset(&_resources, 0, sizeof(_resources));

	_device._lock.ReadOnlyCache.LockRead();
}

CommandBuffer::~CommandBuffer() noexcept {
	_device._lock.ReadOnlyCache.UnlockRead();
}

void CommandBuffer::Begin() {
	const vk::CommandBufferBeginInfo beginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit, nullptr);
	_commandBuffer.begin(beginInfo);
}

void CommandBuffer::End() {
	EndThread();
}

void CommandBuffer::EndThread() {
	if (_ended) { return; }
	_ended = true;

	Log::Assert(_threadIndex == Threading::GetThreadID(),
	            "Vulkan::CommandBuffer",
	            "Command Buffer ended on a different thread than it began on");

	_commandBuffer.end();
}

void CommandBuffer::Barrier(const vk::DependencyInfo& dependency) {
	_commandBuffer.pipelineBarrier2(dependency);
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

void CommandBuffer::CopyBufferToImage(const Image& dst,
                                      const Buffer& src,
                                      const std::vector<vk::BufferImageCopy>& blits) {
	_commandBuffer.copyBufferToImage(
		src.GetBuffer(), dst.GetImage(), dst.GetLayout(vk::ImageLayout::eTransferDstOptimal), blits);

	if (dst.IsSwapchainImage()) { _swapchainStages |= vk::PipelineStageFlagBits2::eCopy; }
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

	if (dst.IsSwapchainImage()) { _swapchainStages |= vk::PipelineStageFlagBits2::eCopy; }
}

void CommandBuffer::FillBuffer(const Buffer& dst, uint8_t value) {
	FillBuffer(dst, value, 0, VK_WHOLE_SIZE);
}

void CommandBuffer::FillBuffer(const Buffer& dst, uint8_t value, vk::DeviceSize offset, vk::DeviceSize size) {
	_commandBuffer.fillBuffer(dst.GetBuffer(), offset, size, value);
}

void CommandBuffer::UpdateBuffer(const Buffer& dst, size_t dataSize, const void* data, vk::DeviceSize offset) {
	_commandBuffer.updateBuffer(dst.GetBuffer(), offset, dataSize, data);
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

	if (dst.IsSwapchainImage()) { _swapchainStages |= vk::PipelineStageFlagBits2::eBlit; }
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

void CommandBuffer::PushConstants(size_t size, const void* data, vk::DeviceSize offset) {
	memcpy(&_resources.PushConstantData[offset], data, size);
	_dirty |= CommandBufferDirtyFlagBits::PushConstants;
}

void CommandBuffer::SetStorageBuffer(uint32_t set, uint32_t binding, const Buffer& buffer) {
	SetStorageBuffer(set, binding, buffer, 0, buffer.GetCreateInfo().Size);
}

void CommandBuffer::SetStorageBuffer(
	uint32_t set, uint32_t binding, const Buffer& buffer, vk::DeviceSize offset, vk::DeviceSize range) {
	auto& bind = _resources.Bindings[set][binding];

	if (buffer.GetCookie() == bind.Cookie && bind.Buffer.offset == offset && bind.Buffer.range == range) { return; }

	bind.Buffer          = vk::DescriptorBufferInfo(buffer.GetBuffer(), offset, range);
	bind.DynamicOffset   = 0;
	bind.Cookie          = buffer.GetCookie();
	bind.SecondaryCookie = 0;

	_dirtySets |= 1u << set;
}

void CommandBuffer::SetUniformBuffer(
	uint32_t set, uint32_t binding, const Buffer& buffer, vk::DeviceSize offset, vk::DeviceSize range) {
	if (range == 0) { range = buffer.GetCreateInfo().Size; }

	auto& bind = _resources.Bindings[set][binding];

	if (buffer.GetCookie() == _resources.Bindings[set][binding].Cookie && bind.Buffer.range == range) {
		if (bind.DynamicOffset != offset) {
			_dirtySetsDynamic |= 1u << set;
			bind.DynamicOffset = offset;
		}
	} else {
		bind.Buffer                                       = vk::DescriptorBufferInfo(buffer.GetBuffer(), 0, range);
		bind.DynamicOffset                                = offset;
		_resources.Bindings[set][binding].Cookie          = buffer.GetCookie();
		_resources.Bindings[set][binding].SecondaryCookie = 0;
		_dirtySets |= 1u << set;
	}
}

void CommandBuffer::Dispatch(uint32_t groupsX, uint32_t groupsY, uint32_t groupsZ) {
	if (FlushComputeState(true)) { _commandBuffer.dispatch(groupsX, groupsY, groupsZ); }
}

void CommandBuffer::DispatchIndirect(const Buffer& buffer, vk::DeviceSize offset) {
	if (FlushComputeState(true)) { _commandBuffer.dispatchIndirect(buffer.GetBuffer(), offset); }
}

void CommandBuffer::Draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) {
	if (FlushRenderState(true)) { _commandBuffer.draw(vertexCount, instanceCount, firstVertex, firstInstance); }
}

void CommandBuffer::DrawIndexed(
	uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance) {
	if (FlushRenderState(true)) {
		_commandBuffer.drawIndexed(indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
	}
}

void CommandBuffer::DrawIndexedIndirect(const Buffer& buffer,
                                        uint32_t drawCount,
                                        vk::DeviceSize offset,
                                        vk::DeviceSize stride) {
	if (FlushRenderState(true)) { _commandBuffer.drawIndexedIndirect(buffer.GetBuffer(), offset, drawCount, stride); }
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

#define SetStaticState(field, value)                                        \
	do {                                                                      \
		if (_pipelineState.StaticState.field != static_cast<unsigned>(value)) { \
			_pipelineState.StaticState.field = static_cast<unsigned>(value);      \
			_dirty |= CommandBufferDirtyFlagBits::StaticState;                    \
		}                                                                       \
	} while (0)

#define SetDynamicState(field, value, dirtyFlag) \
	do {                                           \
		if (_dynamicState.field != value) {          \
			_dynamicState.field = value;               \
			_dirty |= dirtyFlag;                       \
		}                                            \
	} while (0)

void CommandBuffer::SetAlphaBlend(vk::BlendFactor srcAlpha, vk::BlendOp op, vk::BlendFactor dstAlpha) {
	SetStaticState(SrcAlphaBlend, srcAlpha);
	SetStaticState(AlphaBlendOp, op);
	SetStaticState(DstAlphaBlend, dstAlpha);
}

void CommandBuffer::SetBlendEnable(bool enable) {
	SetStaticState(BlendEnable, enable);
}

void CommandBuffer::SetColorBlend(vk::BlendFactor srcColor, vk::BlendOp op, vk::BlendFactor dstColor) {
	SetStaticState(SrcColorBlend, srcColor);
	SetStaticState(ColorBlendOp, op);
	SetStaticState(DstColorBlend, dstColor);
}

void CommandBuffer::SetColorWriteMask(uint32_t mask) {
	SetStaticState(WriteMask, mask);
}

void CommandBuffer::SetCullMode(vk::CullModeFlagBits mode) {
	SetStaticState(CullMode, mode);
}

void CommandBuffer::SetDepthCompareOp(vk::CompareOp op) {
	SetStaticState(DepthCompare, op);
}

void CommandBuffer::SetDepthTest(bool test) {
	SetStaticState(DepthTest, test);
}

void CommandBuffer::SetDepthWrite(bool write) {
	SetStaticState(DepthWrite, write);
}

void CommandBuffer::SetFrontFace(vk::FrontFace face) {
	SetStaticState(FrontFace, face);
}

void CommandBuffer::SetPrimitiveTopology(vk::PrimitiveTopology topology) {
	SetStaticState(Topology, topology);
}

void CommandBuffer::SetSampler(uint32_t set, uint32_t binding, const Sampler& sampler) {
	const auto cookie = sampler.GetCookie();
	if (cookie == _resources.Bindings[set][binding].SecondaryCookie) { return; }

	auto& bind                 = _resources.Bindings[set][binding];
	bind.Image.Float.sampler   = sampler.GetSampler();
	bind.Image.Integer.sampler = sampler.GetSampler();
	_dirtySets |= 1u << set;
	_resources.Bindings[set][binding].SecondaryCookie = cookie;
}

void CommandBuffer::SetSampler(uint32_t set, uint32_t binding, StockSampler sampler) {
	SetSampler(set, binding, _device.GetStockSampler(sampler));
}

void CommandBuffer::SetTexture(uint32_t set, uint32_t binding, const ImageView& view) {
	SetTexture(set,
	           binding,
	           view.GetFloatView(),
	           view.GetIntegerView(),
	           view.GetImage().GetLayout(vk::ImageLayout::eShaderReadOnlyOptimal),
	           view.GetCookie());
}

void CommandBuffer::SetTexture(uint32_t set, uint32_t binding, const ImageView& view, const Sampler& sampler) {
	SetTexture(set, binding, view);
	SetSampler(set, binding, sampler);
}

void CommandBuffer::SetTexture(uint32_t set, uint32_t binding, const ImageView& view, StockSampler sampler) {
	SetTexture(set, binding, view);
	SetSampler(set, binding, sampler);
}

void CommandBuffer::SetSrgbTexture(uint32_t set, uint32_t binding, const ImageView& view) {
	const auto srgbView = view.GetSrgbView();
	SetTexture(set,
	           binding,
	           srgbView,
	           srgbView,
	           view.GetImage().GetLayout(vk::ImageLayout::eShaderReadOnlyOptimal),
	           view.GetCookie() | CookieSrgbBit);
}

void CommandBuffer::SetSrgbTexture(uint32_t set, uint32_t binding, const ImageView& view, const Sampler& sampler) {
	SetSrgbTexture(set, binding, view);
	SetSampler(set, binding, sampler);
}

void CommandBuffer::SetSrgbTexture(uint32_t set, uint32_t binding, const ImageView& view, StockSampler sampler) {
	SetSrgbTexture(set, binding, view);
	SetSampler(set, binding, sampler);
}

void CommandBuffer::SetUnormTexture(uint32_t set, uint32_t binding, const ImageView& view) {
	const auto unormView = view.GetUnormView();
	SetTexture(set,
	           binding,
	           unormView,
	           unormView,
	           view.GetImage().GetLayout(vk::ImageLayout::eShaderReadOnlyOptimal),
	           view.GetCookie() | CookieUnormBit);
}

void CommandBuffer::SetUnormTexture(uint32_t set, uint32_t binding, const ImageView& view, const Sampler& sampler) {
	SetUnormTexture(set, binding, view);
	SetSampler(set, binding, sampler);
}

void CommandBuffer::SetUnormTexture(uint32_t set, uint32_t binding, const ImageView& view, StockSampler sampler) {
	SetUnormTexture(set, binding, view);
	SetSampler(set, binding, sampler);
}

void CommandBuffer::SetOpaqueState() {
	ClearRenderState();

	auto& state            = _pipelineState.StaticState;
	state.FrontFace        = int(vk::FrontFace::eCounterClockwise);
	state.CullMode         = int(vk::CullModeFlagBits::eBack);
	state.BlendEnable      = 0;
	state.DepthTest        = 1;
	state.DepthCompare     = int(vk::CompareOp::eLessOrEqual);
	state.DepthWrite       = 1;
	state.DepthBiasEnable  = 0;
	state.PrimitiveRestart = 0;
	state.StencilTest      = 0;
	state.Topology         = int(vk::PrimitiveTopology::eTriangleList);
	state.WriteMask        = ~0u;

	_dirty |= CommandBufferDirtyFlagBits::StaticState;
}

void CommandBuffer::SetTransparentSpriteState() {
	ClearRenderState();
	auto& state            = _pipelineState.StaticState;
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

void CommandBuffer::SetIndexBuffer(const Buffer& buffer, vk::DeviceSize offset, vk::IndexType indexType) {
	if (_indexState.Buffer == buffer.GetBuffer() && _indexState.Offset == offset && _indexState.IndexType == indexType) {
		return;
	}

	_indexState.Buffer    = buffer.GetBuffer();
	_indexState.Offset    = offset;
	_indexState.IndexType = indexType;
	_commandBuffer.bindIndexBuffer(_indexState.Buffer, _indexState.Offset, _indexState.IndexType);
}

void CommandBuffer::SetProgram(Program* program) {
	if (_pipelineState.Program == program) { return; }

	_pipelineState.Program = program;
	_currentPipeline       = {};

	_dirty |= CommandBufferDirtyFlagBits::Pipeline;
	if (!program) { return; }

	if (!_pipelineState.PipelineLayout) {
		_dirty |= CommandBufferDirtyFlagBits::PushConstants;
		_dirtySets = ~0u;
	} else if (program->GetPipelineLayout()->GetHash() != _pipelineState.PipelineLayout->GetHash()) {
		auto& newLayout = program->GetPipelineLayout()->GetResourceLayout();
		auto& oldLayout = _pipelineState.PipelineLayout->GetResourceLayout();

		if (newLayout.PushConstantLayoutHash != oldLayout.PushConstantLayoutHash) {
			_dirty |= CommandBufferDirtyFlagBits::PushConstants;
			_dirtySets = ~0u;
		} else {
			auto* newPipelineLayout = program->GetPipelineLayout();
			for (uint32_t set = 0; set < MaxDescriptorSets; ++set) {
				if (newPipelineLayout->GetAllocator(set) != _pipelineState.PipelineLayout->GetAllocator(set)) {
					_dirtySets |= ~((1u << set) - 1);
					break;
				}
			}
		}
	}

	_pipelineState.PipelineLayout = program->GetPipelineLayout();
	_pipelineLayout               = _pipelineState.PipelineLayout->GetPipelineLayout();
}

void CommandBuffer::SetScissor(const vk::Rect2D& scissor) {
	_scissor = scissor;
	_dirty |= CommandBufferDirtyFlagBits::Scissor;
}

void CommandBuffer::SetTexture(uint32_t set,
                               uint32_t binding,
                               vk::ImageView floatView,
                               vk::ImageView integerView,
                               vk::ImageLayout layout,
                               uint64_t cookie) {
	if (cookie == _resources.Bindings[set][binding].Cookie &&
	    _resources.Bindings[set][binding].Image.Float.imageLayout == layout) {
		return;
	}

	auto& bind                     = _resources.Bindings[set][binding];
	bind.Image.Float.imageLayout   = layout;
	bind.Image.Float.imageView     = floatView;
	bind.Image.Integer.imageLayout = layout;
	bind.Image.Integer.imageView   = integerView;
	bind.Cookie                    = cookie;

	_dirtySets |= 1u << set;
}

void CommandBuffer::SetVertexAttribute(uint32_t attribute, uint32_t binding, vk::Format format, vk::DeviceSize offset) {
	auto& attr = _pipelineState.Attributes[attribute];

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
		_dirtyVBOs |= 1u << binding;
	}
	if (_pipelineState.Strides[binding] != stride || _pipelineState.InputRates[binding] != inputRate) {
		_dirty |= CommandBufferDirtyFlagBits::StaticVertex;
	}

	_vertexBindings.Buffers[binding]   = vkBuffer;
	_vertexBindings.Offsets[binding]   = offset;
	_pipelineState.InputRates[binding] = inputRate;
	_pipelineState.Strides[binding]    = stride;
}

void CommandBuffer::BeginCompute() {
	_isCompute = true;
	BeginContext();
}

void CommandBuffer::BeginContext() {
	_dirty                                                       = ~CommandBufferDirtyFlags{};
	_dirtySets                                                   = ~0u;
	_dirtyVBOs                                                   = ~0u;
	_currentPipeline                                             = {};
	_pipelineLayout                                              = nullptr;
	_pipelineState.PipelineLayout                                = nullptr;
	_pipelineState.Program                                       = nullptr;
	_pipelineState.PotentialStaticState.SpecConstantMask         = 0;
	_pipelineState.PotentialStaticState.InternalSpecConstantMask = 0;
	for (uint32_t set = 0; set < MaxDescriptorSets; ++set) {
		for (uint32_t binding = 0; binding < MaxDescriptorBindings; ++binding) {
			_resources.Bindings[set][binding].Cookie          = 0;
			_resources.Bindings[set][binding].SecondaryCookie = 0;
		}
	}
	_vertexBindings.Buffers.fill(VK_NULL_HANDLE);
	memset(&_indexState, 0, sizeof(_indexState));
}

void CommandBuffer::BeginGraphics() {
	_isCompute = false;
	BeginContext();
}

void CommandBuffer::BindPipeline(vk::PipelineBindPoint bindPoint,
                                 vk::Pipeline pipeline,
                                 CommandBufferDirtyFlags activeDynamicState) {
	_commandBuffer.bindPipeline(bindPoint, pipeline);
	_dirty |= (~activeDynamicState) & CommandBufferDirtyFlagBits::Dynamic;
}

Pipeline CommandBuffer::BuildComputePipeline(bool synchronous) {
	RWSpinLockReadHolder guard(_device._lock.ReadOnlyCache);

	if (!synchronous) { return {}; }

	auto& shader = *_pipelineState.Program->GetShader(ShaderStage::Compute);

	vk::PipelineShaderStageCreateInfo stage({}, vk::ShaderStageFlagBits::eCompute, shader.GetShader(), "main", nullptr);

	vk::SpecializationInfo specInfo = {};
	std::array<vk::SpecializationMapEntry, MaxSpecConstants> specMap;
	std::array<uint32_t, MaxSpecConstants> specConstants;
	const auto specMask = _pipelineState.PipelineLayout->GetResourceLayout().CombinedSpecConstantMask &
	                      CombinedSpecConstantMask(_pipelineState);
	if (specMask) {
		stage.pSpecializationInfo = &specInfo;
		specInfo.pData            = specConstants.data();
		specInfo.pMapEntries      = specMap.data();

		ForEachBit(specMask, [&](uint32_t bit) {
			auto& entry = specMap[specInfo.mapEntryCount];
			entry       = vk::SpecializationMapEntry(bit, sizeof(uint32_t) * specInfo.mapEntryCount, sizeof(uint32_t));
			specConstants[specInfo.mapEntryCount] = _pipelineState.PotentialStaticState.SpecConstants[bit];
			specInfo.mapEntryCount++;
		});
		specInfo.dataSize = specInfo.mapEntryCount * sizeof(uint32_t);
	}

	const vk::ComputePipelineCreateInfo pipelineCI({}, stage, _pipelineState.PipelineLayout->GetPipelineLayout());
	const auto result = _device.GetDevice().createComputePipeline(nullptr, pipelineCI);
	if (result.result != vk::Result::eSuccess) {
		Log::Error("CommandBuffer", "Failed to create compute pipeline: {}", vk::to_string(result.result));

		return {};
	}

	auto returnPipeline = _pipelineState.Program->AddPipeline(_pipelineState.CachedHash, {result.value, {}});
	if (returnPipeline.Pipeline != result.value) { _device.GetDevice().destroyPipeline(result.value); }

	return returnPipeline;
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
	                                    _pipelineState.Program->GetShader(ShaderStage::Vertex)->GetShader(),
	                                    "main",
	                                    nullptr));
	if (_pipelineState.Program->GetShader(ShaderStage::TessellationControl)) {
		hasTessellation = true;
		stages.push_back(vk::PipelineShaderStageCreateInfo(
			{},
			vk::ShaderStageFlagBits::eTessellationControl,
			_pipelineState.Program->GetShader(ShaderStage::TessellationControl)->GetShader(),
			"main",
			nullptr));
	}
	if (_pipelineState.Program->GetShader(ShaderStage::TessellationEvaluation)) {
		hasTessellation = true;
		stages.push_back(vk::PipelineShaderStageCreateInfo(
			{},
			vk::ShaderStageFlagBits::eTessellationEvaluation,
			_pipelineState.Program->GetShader(ShaderStage::TessellationEvaluation)->GetShader(),
			"main",
			nullptr));
	}
	stages.push_back(
		vk::PipelineShaderStageCreateInfo({},
	                                    vk::ShaderStageFlagBits::eFragment,
	                                    _pipelineState.Program->GetShader(ShaderStage::Fragment)->GetShader(),
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
	const auto pipelineResult = _device.GetDevice().createGraphicsPipeline(nullptr, pipelineCI);
	const auto returnedPipeline =
		_pipelineState.Program->AddPipeline(_pipelineState.CachedHash, {pipelineResult.value, {}});
	if (returnedPipeline.Pipeline != pipelineResult.value) { _device.GetDevice().destroyPipeline(pipelineResult.value); }

	return returnedPipeline;
}

void CommandBuffer::ClearRenderState() {
	memset(&_pipelineState.StaticState, 0, sizeof(_pipelineState.StaticState));
}

bool CommandBuffer::FlushComputePipeline(bool synchronous) {
	const auto pipelineHash = _pipelineState.GetComputeHash();
	_currentPipeline        = _pipelineState.Program->GetPipeline(pipelineHash);
	if (!_currentPipeline.Pipeline) { _currentPipeline = BuildComputePipeline(synchronous); }

	return bool(_currentPipeline.Pipeline);
}

vk::Pipeline CommandBuffer::FlushComputeState(bool synchronous) {
	if (!_pipelineState.Program) { return nullptr; }

	if (!_currentPipeline.Pipeline) { _dirty |= CommandBufferDirtyFlagBits::Pipeline; }

	if (_dirty & (CommandBufferDirtyFlagBits::StaticState | CommandBufferDirtyFlagBits::Pipeline)) {
		vk::Pipeline oldPipeline = _currentPipeline.Pipeline;
		if (!FlushComputePipeline(synchronous)) { return nullptr; }
		if (oldPipeline != _currentPipeline.Pipeline) {
			BindPipeline(vk::PipelineBindPoint::eCompute, _currentPipeline.Pipeline, _currentPipeline.DynamicMask);
		}
	}
	_dirty &= ~(CommandBufferDirtyFlagBits::StaticState | CommandBufferDirtyFlagBits::Pipeline);

	if (!_currentPipeline.Pipeline) { return nullptr; }

	FlushDescriptorSets();

	if (_dirty & CommandBufferDirtyFlagBits::PushConstants) {
		auto& range = _pipelineState.PipelineLayout->GetResourceLayout().PushConstantRange;
		if (range.stageFlags) {
			_commandBuffer.pushConstants(_pipelineLayout, range.stageFlags, 0, range.size, _resources.PushConstantData);
		}
	}
	_dirty &= ~(CommandBufferDirtyFlagBits::PushConstants);

	return _currentPipeline.Pipeline;
}

void CommandBuffer::FlushDescriptorSet(uint32_t set) {
	auto& layout = _pipelineState.PipelineLayout->GetResourceLayout();
	if (layout.BindlessDescriptorSetMask & (1u << set)) {
		/*
		_commandBuffer.bindDescriptorSets(
		  _actualRenderPass ? vk::PipelineBindPoint::eGraphics : vk::PipelineBindPoint::eCompute,
		  _pipelineLayout,
		  set,
		  1,
		  &_bindlessSets[set],
		  0,
		  nullptr);
		*/

		return;
	}

	auto& setLayout                                = layout.SetLayouts[set];
	uint32_t dynamicOffsetCount                    = 0;
	uint32_t dynamicOffsets[MaxDescriptorBindings] = {0};

	Hasher h;
	h(setLayout.FloatMask);

	ForEachBit(setLayout.InputAttachmentMask, [&](uint32_t binding) {
		auto arraySize = setLayout.ArraySizes[binding];
		for (uint32_t i = 0; i < arraySize; ++i) {
			h(_resources.Bindings[set][binding + i].Cookie);
			h(_resources.Bindings[set][binding + i].Image.Float.imageLayout);
		}
	});

	ForEachBit(setLayout.SampledImageMask, [&](uint32_t binding) {
		auto arraySize = setLayout.ArraySizes[binding];
		for (uint32_t i = 0; i < arraySize; ++i) {
			h(_resources.Bindings[set][binding + i].Cookie);
			h(_resources.Bindings[set][binding + i].Image.Float.imageLayout);
		}
	});

	ForEachBit(setLayout.SampledTexelBufferMask, [&](uint32_t binding) {
		auto arraySize = setLayout.ArraySizes[binding];
		for (uint32_t i = 0; i < arraySize; ++i) { h(_resources.Bindings[set][binding + i].Cookie); }
	});

	ForEachBit(setLayout.SamplerMask, [&](uint32_t binding) {
		auto arraySize = setLayout.ArraySizes[binding];
		for (uint32_t i = 0; i < arraySize; ++i) { h(_resources.Bindings[set][binding + i].Cookie); }
	});

	ForEachBit(setLayout.SeparateImageMask, [&](uint32_t binding) {
		auto arraySize = setLayout.ArraySizes[binding];
		for (uint32_t i = 0; i < arraySize; ++i) {
			h(_resources.Bindings[set][binding + i].Cookie);
			h(_resources.Bindings[set][binding + i].Image.Float.imageLayout);
		}
	});

	ForEachBit(setLayout.StorageBufferMask, [&](uint32_t binding) {
		auto arraySize = setLayout.ArraySizes[binding];
		for (uint32_t i = 0; i < arraySize; ++i) {
			h(_resources.Bindings[set][binding + i].Cookie);
			h(_resources.Bindings[set][binding + i].Buffer.offset);
			h(_resources.Bindings[set][binding + i].Buffer.range);
		}
	});

	ForEachBit(setLayout.StorageImageMask, [&](uint32_t binding) {
		auto arraySize = setLayout.ArraySizes[binding];
		for (uint32_t i = 0; i < arraySize; ++i) {
			h(_resources.Bindings[set][binding + i].Cookie);
			h(_resources.Bindings[set][binding + i].Image.Float.imageLayout);
		}
	});

	ForEachBit(setLayout.UniformBufferMask, [&](uint32_t binding) {
		auto arraySize = setLayout.ArraySizes[binding];
		for (uint32_t i = 0; i < arraySize; ++i) {
			h(_resources.Bindings[set][binding + i].Cookie);
			h(_resources.Bindings[set][binding + i].Buffer.range);
			dynamicOffsets[dynamicOffsetCount++] = _resources.Bindings[set][binding + i].DynamicOffset;
		}
	});

	const auto hash = h.Get();
	auto allocated  = _pipelineState.PipelineLayout->GetAllocator(set)->Find(_threadIndex, hash);
	if (!allocated.second) {
		auto updateTemplate = _pipelineState.PipelineLayout->GetUpdateTemplate(set);
		_device.GetDevice().updateDescriptorSetWithTemplate(allocated.first, updateTemplate, _resources.Bindings[set]);
	}

	_commandBuffer.bindDescriptorSets(
		_actualRenderPass ? vk::PipelineBindPoint::eGraphics : vk::PipelineBindPoint::eCompute,
		_pipelineLayout,
		set,
		1,
		&allocated.first,
		dynamicOffsetCount,
		dynamicOffsets);
	_allocatedSets[set] = allocated.first;
}

void CommandBuffer::FlushDescriptorSets() {
	auto& layout = _pipelineState.PipelineLayout->GetResourceLayout();

	uint32_t setUpdate = layout.DescriptorSetMask & _dirtySets;
	ForEachBit(setUpdate, [&](uint32_t set) { FlushDescriptorSet(set); });
	_dirtySets &= ~setUpdate;
	_dirtySetsDynamic &= ~setUpdate;

	uint32_t dynamicSetUpdate = layout.DescriptorSetMask & _dirtySetsDynamic;
	ForEachBit(dynamicSetUpdate, [&](uint32_t set) { RebindDescriptorSet(set); });
	_dirtySetsDynamic &= ~dynamicSetUpdate;
}

bool CommandBuffer::FlushGraphicsPipeline(bool synchronous) {
	_pipelineState.CachedHash = _pipelineState.GetHash(_activeVBOs);
	_currentPipeline          = _pipelineState.Program->GetPipeline(_pipelineState.CachedHash);
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
	FlushDescriptorSets();

	if (_dirty & CommandBufferDirtyFlagBits::PushConstants) {
		const auto& range = _pipelineState.PipelineLayout->GetResourceLayout().PushConstantRange;
		if (range.stageFlags) {
			_commandBuffer.pushConstants(_pipelineLayout, range.stageFlags, 0, range.size, _resources.PushConstantData);
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

void CommandBuffer::RebindDescriptorSet(uint32_t set) {
	auto& layout = _pipelineState.PipelineLayout->GetResourceLayout();
	if (layout.BindlessDescriptorSetMask & (1u << set)) {}

	auto& setLayout             = layout.SetLayouts[set];
	uint32_t dynamicOffsetCount = 0;
	std::array<uint32_t, MaxDescriptorBindings> dynamicOffsets;

	ForEachBit(setLayout.UniformBufferMask, [&](uint32_t binding) {
		const auto arraySize = setLayout.ArraySizes[binding];
		for (uint32_t i = 0; i < arraySize; ++i) {
			dynamicOffsets[dynamicOffsetCount++] = _resources.Bindings[set][binding + i].DynamicOffset;
		}
	});

	_commandBuffer.bindDescriptorSets(
		_actualRenderPass ? vk::PipelineBindPoint::eGraphics : vk::PipelineBindPoint::eCompute,
		_pipelineLayout,
		set,
		1,
		&_allocatedSets[set],
		dynamicOffsetCount,
		dynamicOffsets.data());
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
