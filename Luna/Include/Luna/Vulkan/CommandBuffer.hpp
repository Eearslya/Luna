#pragma once

#include <Luna/Vulkan/BufferPool.hpp>
#include <Luna/Vulkan/Common.hpp>
#include <Luna/Vulkan/Tracing.hpp>

namespace Luna {
namespace Vulkan {
static constexpr uint64_t CookieUnormBit = 1 << 0;
static constexpr uint64_t CookieSrgbBit  = 1 << 1;

static constexpr int BlendFactorBits = 5;
static constexpr int BlendOpBits     = 3;
static constexpr int CompareOpBits   = 3;
static constexpr int CullModeBits    = 2;
static constexpr int FrontFaceBits   = 1;
static constexpr int StencilOpBits   = 3;
union PipelineState {
	struct {
		// Depth Testing
		unsigned DepthBiasEnable : 1;
		unsigned DepthCompare : CompareOpBits;
		unsigned DepthTest : 1;
		unsigned DepthWrite : 1;

		// Stencil Testing
		unsigned StencilBackCompareOp : CompareOpBits;
		unsigned StencilBackDepthFail : StencilOpBits;
		unsigned StencilBackFail : StencilOpBits;
		unsigned StencilBackPass : StencilOpBits;
		unsigned StencilFrontCompareOp : CompareOpBits;
		unsigned StencilFrontDepthFail : StencilOpBits;
		unsigned StencilFrontFail : StencilOpBits;
		unsigned StencilFrontPass : StencilOpBits;
		unsigned StencilTest : 1;

		// Culling
		unsigned CullMode : CullModeBits;
		unsigned FrontFace : FrontFaceBits;

		// Blending
		unsigned AlphaBlendOp : BlendOpBits;
		unsigned BlendEnable : 1;
		unsigned ColorBlendOp : BlendOpBits;
		unsigned DstAlphaBlend : BlendFactorBits;
		unsigned DstColorBlend : BlendFactorBits;
		unsigned SrcAlphaBlend : BlendFactorBits;
		unsigned SrcColorBlend : BlendFactorBits;

		// Sample Shading
		unsigned AlphaToCoverage : 1;
		unsigned AlphaToOne : 1;
		unsigned ConservativeRaster : 1;
		unsigned SampleShading : 1;

		// Topology
		unsigned PrimitiveRestart : 1;
		unsigned Topology : 4;
		unsigned Wireframe : 1;

		// Subgroups
		unsigned SubgroupControlSize : 1;
		unsigned SubgroupFullGroup : 1;
		unsigned SubgroupMinimumSizeLog2 : 3;
		unsigned SubgroupMaximumSizeLog2 : 3;

		uint32_t WriteMask;
	};

	uint32_t Words[4];
};

struct PotentialState {
	std::array<float, 4> BlendConstants                  = {0.0f};
	std::array<uint32_t, MaxSpecConstants> SpecConstants = {0};
	uint8_t SpecConstantMask                             = 0;
	uint8_t InternalSpecConstantMask                     = 0;
};

struct DynamicState {
	float DepthBiasConstant  = 0.0f;
	float DepthBiasSlope     = 0.0f;
	uint8_t FrontCompareMask = 0;
	uint8_t FrontWriteMask   = 0;
	uint8_t FrontReference   = 0;
	uint8_t BackCompareMask  = 0;
	uint8_t BackWriteMask    = 0;
	uint8_t BackReference    = 0;
};

struct VertexAttributeState {
	uint32_t Binding  = 0;
	vk::Format Format = vk::Format::eUndefined;
	uint32_t Offset   = 0;
};

struct VertexBindingState {
	std::array<vk::Buffer, MaxVertexBindings> Buffers;
	std::array<vk::DeviceSize, MaxVertexBindings> Offsets = {0};
};

struct IndexState {
	vk::Buffer Buffer;
	vk::DeviceSize Offset   = 0;
	vk::IndexType IndexType = vk::IndexType::eUint32;
};

struct DeferredPipelineCompile {
	Program* Program                       = nullptr;
	const RenderPass* CompatibleRenderPass = nullptr;

	PipelineState StaticState           = {};
	PotentialState PotentialStaticState = {};

	std::array<VertexAttributeState, MaxVertexAttributes> Attributes = {};
	std::array<vk::VertexInputRate, MaxVertexBindings> InputRates    = {vk::VertexInputRate::eVertex};
	std::array<vk::DeviceSize, MaxVertexBindings> Strides            = {0};

	uint32_t SubpassIndex = 0;
	Hash Hash;
	uint32_t SubgroupSizeTag = 0;

	::Luna::Hash GetComputeHash() const;
	::Luna::Hash GetHash(uint32_t& activeVBOs) const;
};

struct CommandBufferDeleter {
	void operator()(CommandBuffer* cmdBuf);
};

class CommandBuffer : public IntrusivePtrEnabled<CommandBuffer, CommandBufferDeleter, HandleCounter> {
	friend class ObjectPool<CommandBuffer>;
	friend struct CommandBufferDeleter;

 public:
	class TracingZone {
	 public:
		TracingZone(CommandBuffer& parent, const std::string& name) : _parent(parent) {
			_parent.BeginZone(name);
		}

		~TracingZone() noexcept {
			_parent.EndZone();
		}

	 private:
		CommandBuffer& _parent;
	};

	~CommandBuffer() noexcept;

	vk::CommandBuffer GetCommandBuffer() const {
		return _commandBuffer;
	}
	Device& GetDevice() {
		return _device;
	}
	vk::PipelineStageFlags2 GetSwapchainStages() const {
		return _swapchainStages;
	}
	TracyVkCtx GetTracingContext() const {
		return _tracingContext;
	}
	CommandBufferType GetType() const {
		return _commandBufferType;
	}
	TracingZone Zone(const std::string& name) {
		return TracingZone(*this, name);
	}

	void BeginZone(const std::string& name);
	void End();
	void EndZone();
	void TouchSwapchain(vk::PipelineStageFlags2 stages);

	void Barrier(const vk::DependencyInfo& dep);
	void BarrierPrepareGenerateMipmaps(const Image& image,
	                                   vk::ImageLayout baseLevelLayout,
	                                   vk::PipelineStageFlags2 srcStages,
	                                   vk::AccessFlags2 srcAccess,
	                                   bool needTopLevelBarrier = true);
	void ImageBarrier(const Image& image,
	                  vk::ImageLayout oldLayout,
	                  vk::ImageLayout newLayout,
	                  vk::PipelineStageFlags2 srcStages,
	                  vk::AccessFlags2 srcAccess,
	                  vk::PipelineStageFlags2 dstStages,
	                  vk::AccessFlags2 dstAccess);
	void ImageBarriers(const std::vector<vk::ImageMemoryBarrier2>& barriers);

	void BlitImage(const Image& dst,
	               const Image& src,
	               const vk::Offset3D& dstOffset0,
	               const vk::Offset3D& dstExtent,
	               const vk::Offset3D& srcOffset0,
	               const vk::Offset3D& srcExtent,
	               uint32_t dstLevel,
	               uint32_t srcLevel,
	               uint32_t dstBaseLayer = 0,
	               uint32_t srcBaseLayer = 0,
	               uint32_t layerCount   = 1,
	               vk::Filter filter     = vk::Filter::eLinear);
	void ClearColorImage(const Image& image, const vk::ClearColorValue& clear);
	void ClearColorImage(const Image& image,
	                     const vk::ClearColorValue& clear,
	                     const std::vector<vk::ImageSubresourceRange>& ranges);
	void CopyBuffer(const Buffer& dst, const Buffer& src);
	void CopyBuffer(
		const Buffer& dst, vk::DeviceSize dstOffset, const Buffer& src, vk::DeviceSize srcOffset, vk::DeviceSize size);
	void CopyBuffer(const Buffer& dst, const Buffer& src, const std::vector<vk::BufferCopy>& copies);
	void CopyBufferToImage(const Image& dst, const Buffer& src, const std::vector<vk::BufferImageCopy>& blits);
	void CopyBufferToImage(const Image& dst,
	                       const Buffer& src,
	                       vk::DeviceSize bufferOffset,
	                       const vk::Offset3D& offset,
	                       const vk::Extent3D& extent,
	                       uint32_t rowLength,
	                       uint32_t sliceHeight,
	                       const vk::ImageSubresourceLayers& subresource);
	void CopyImage(Image& dst,
	               Image& src,
	               const vk::Offset3D& dstOffset,
	               const vk::Offset3D& srcOffset,
	               const vk::Extent3D& extent,
	               const vk::ImageSubresourceLayers& dstSubresource,
	               const vk::ImageSubresourceLayers& srcSubresource);
	void FillBuffer(const Buffer& dst, uint8_t value);
	void FillBuffer(const Buffer& dst, uint8_t value, vk::DeviceSize offset, vk::DeviceSize size);
	void GenerateMipmaps(const Image& image);

	void ClearRenderState();
	void SetOpaqueState();
	void SetTransparentSpriteState();

	void SetBlendEnable(bool enable);
	void SetColorBlend(vk::BlendFactor srcColor, vk::BlendOp op, vk::BlendFactor dstColor);
	void SetCullMode(vk::CullModeFlagBits mode);
	void SetDepthCompareOp(vk::CompareOp op);
	void SetDepthWrite(bool write);

	void Dispatch(uint32_t groupsX, uint32_t groupsY, uint32_t groupsZ);
	void Draw(uint32_t vertexCount, uint32_t instanceCount = 1, uint32_t firstVertex = 0, uint32_t firstInstance = 0);
	void DrawIndexed(uint32_t indexCount,
	                 uint32_t instanceCount = 1,
	                 uint32_t firstIndex    = 0,
	                 int32_t vertexOffset   = 0,
	                 uint32_t firstInstance = 0);
	void DrawIndexedIndirect(const Vulkan::Buffer& buffer,
	                         vk::DeviceSize offset,
	                         uint32_t drawCount,
	                         vk::DeviceSize stride);
	void PushConstants(const void* data, vk::DeviceSize offset, vk::DeviceSize range);
	void SetBindless(uint32_t set, vk::DescriptorSet descSet);
	void SetIndexBuffer(const Buffer& buffer, vk::DeviceSize offset, vk::IndexType indexType);
	void SetInputAttachments(uint32_t set, uint32_t firstBinding);
	void SetProgram(Program* program);
	void SetScissor(const vk::Rect2D& scissor);

	void SetSampler(uint32_t set, uint32_t binding, const Sampler& sampler);
	void SetSampler(uint32_t set, uint32_t binding, StockSampler sampler);

	void SetTexture(uint32_t set, uint32_t binding, const ImageView& view);
	void SetTexture(uint32_t set, uint32_t binding, const ImageView& view, const Sampler& sampler);
	void SetTexture(uint32_t set, uint32_t binding, const ImageView& view, StockSampler sampler);
	void SetSrgbTexture(uint32_t set, uint32_t binding, const ImageView& view);
	void SetSrgbTexture(uint32_t set, uint32_t binding, const ImageView& view, const Sampler& sampler);
	void SetSrgbTexture(uint32_t set, uint32_t binding, const ImageView& view, StockSampler sampler);
	void SetUnormTexture(uint32_t set, uint32_t binding, const ImageView& view);
	void SetUnormTexture(uint32_t set, uint32_t binding, const ImageView& view, const Sampler& sampler);
	void SetUnormTexture(uint32_t set, uint32_t binding, const ImageView& view, StockSampler sampler);

	void SetStorageBuffer(uint32_t set, uint32_t binding, const Buffer& buffer);
	void SetStorageBuffer(
		uint32_t set, uint32_t binding, const Buffer& buffer, vk::DeviceSize offset, vk::DeviceSize range);
	void SetUniformBuffer(
		uint32_t set, uint32_t binding, const Buffer& buffer, vk::DeviceSize offset = 0, vk::DeviceSize range = 0);
	void SetVertexAttribute(uint32_t attribute, uint32_t binding, vk::Format format, vk::DeviceSize offset);
	void SetVertexBinding(uint32_t binding,
	                      const Buffer& buffer,
	                      vk::DeviceSize offset,
	                      vk::DeviceSize stride,
	                      vk::VertexInputRate inputRate);
	void SetViewport(const vk::Viewport& viewport);

	void BeginRenderPass(const RenderPassInfo& info, vk::SubpassContents contents = vk::SubpassContents::eInline);
	void NextSubpass(vk::SubpassContents contents = vk::SubpassContents::eInline);
	void EndRenderPass();

	void* AllocateIndexData(vk::DeviceSize size, vk::IndexType indexType);
	template <typename T>
	T* AllocateTypedIndexData(uint32_t count) {
		if constexpr (std::is_same_v<T, uint32_t>) {
			return static_cast<uint32_t*>(AllocateIndexData(count * sizeof(uint32_t), vk::IndexType::eUint32));
		} else if constexpr (std::is_same_v<T, uint16_t>) {
			return static_cast<uint16_t*>(AllocateIndexData(count * sizeof(uint16_t), vk::IndexType::eUint16));
		} else if constexpr (std::is_same_v<T, uint8_t>) {
			return static_cast<uint8_t*>(AllocateIndexData(count * sizeof(uint16_t), vk::IndexType::eUint8EXT));
		} else {
			return nullptr;
		}
	}
	void* AllocateUniformData(uint32_t set, uint32_t binding, vk::DeviceSize size);
	template <typename T>
	T* AllocateTypedUniformData(uint32_t set, uint32_t binding, uint32_t count) {
		return static_cast<T*>(AllocateUniformData(set, binding, count * sizeof(T)));
	}
	void* AllocateVertexData(uint32_t binding,
	                         vk::DeviceSize size,
	                         vk::DeviceSize stride,
	                         vk::VertexInputRate rate = vk::VertexInputRate::eVertex);

 private:
	CommandBuffer(
		Device& device, vk::CommandBuffer cmdBuf, CommandBufferType type, uint32_t threadIndex, TracyVkCtx tracingContext);

	void BeginCompute();
	void BeginContext();
	void BeginGraphics();
	void BindPipeline(vk::PipelineBindPoint bindPoint, vk::Pipeline pipeline, uint32_t activeDynamicState);
	Pipeline BuildComputePipeline(bool synchronous);
	Pipeline BuildGraphicsPipeline(bool synchronous);
	bool FlushComputePipeline(bool synchronous);
	bool FlushComputeState(bool synchronous);
	void FlushDescriptorSet(uint32_t set);
	void FlushDescriptorSets();
	bool FlushGraphicsPipeline(bool synchronous);
	bool FlushRenderState(bool synchronous);
	void RebindDescriptorSet(uint32_t set);
	void SetTexture(uint32_t set,
	                uint32_t binding,
	                vk::ImageView floatView,
	                vk::ImageView integerView,
	                vk::ImageLayout layout,
	                uint64_t cookie);
	void SetViewportScissor(const RenderPassInfo& info, const Framebuffer* framebuffer);

	Device& _device;
	vk::CommandBuffer _commandBuffer;
	CommandBufferType _commandBufferType;
	uint32_t _threadIndex;
	TracyVkCtx _tracingContext;
	uint32_t _tracingDepth = 0;

	uint32_t _activeVBOs                                            = 0;
	std::array<vk::DescriptorSet, MaxDescriptorSets> _allocatedSets = {};
	ResourceBindings _bindings                                      = {};
	std::array<vk::DescriptorSet, MaxDescriptorSets> _bindlessSets  = {};
	vk::SubpassContents _currentContents                            = vk::SubpassContents::eInline;
	CommandBufferDirtyFlags _dirty                                  = ~0u;
	uint32_t _dirtySets                                             = 0;
	uint32_t _dirtySetsDynamic                                      = 0;
	uint32_t _dirtyVBOs                                             = 0;
	DynamicState _dynamicState                                      = {};
	IndexState _indexState                                          = {};
	bool _isCompute                                                 = true;
	DeferredPipelineCompile _pipelineState                          = {};
	vk::Rect2D _scissor                                             = {};
	vk::PipelineStageFlags2 _swapchainStages                        = {};
	VertexBindingState _vertexBindings                              = {};
	vk::Viewport _viewport                                          = {};

	const RenderPass* _actualRenderPass                                                   = nullptr;
	Pipeline _currentPipeline                                                             = {};
	const Framebuffer* _framebuffer                                                       = nullptr;
	std::array<const Vulkan::ImageView*, MaxColorAttachments + 1> _framebufferAttachments = {nullptr};
	vk::PipelineLayout _pipelineLayout;
	PipelineLayout* _programLayout = nullptr;

	BufferBlock _indexBlock;
	BufferBlock _uniformBlock;
	BufferBlock _vertexBlock;
};
}  // namespace Vulkan
}  // namespace Luna
