#pragma once

#include <Luna/Vulkan/Common.hpp>
#include <Luna/Vulkan/Tracing.hpp>

namespace Luna {
namespace Vulkan {
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

	void SetCullMode(vk::CullModeFlagBits mode);
	void SetDepthCompareOp(vk::CompareOp op);
	void SetDepthWrite(bool write);

	void Draw(uint32_t vertexCount, uint32_t instanceCount = 1, uint32_t firstVertex = 0, uint32_t firstInstance = 0);
	void DrawIndexed(uint32_t indexCount,
	                 uint32_t instanceCount = 1,
	                 uint32_t firstIndex    = 0,
	                 int32_t vertexOffset   = 0,
	                 uint32_t firstInstance = 0);
	void PushConstants(const void* data, vk::DeviceSize offset, vk::DeviceSize range);
	void SetIndexBuffer(const Buffer& buffer, vk::DeviceSize offset, vk::IndexType indexType);
	void SetProgram(Program* program);
	void SetSampler(uint32_t set, uint32_t binding, const Sampler* sampler);
	void SetSampler(uint32_t set, uint32_t binding, StockSampler sampler);
	void SetScissor(const vk::Rect2D& scissor);
	void SetTexture(uint32_t set, uint32_t binding, const ImageView& view);
	void SetTexture(uint32_t set, uint32_t binding, const ImageView& view, const Sampler* sampler);
	void SetTexture(uint32_t set, uint32_t binding, const ImageView& view, StockSampler sampler);
	void SetUniformBuffer(
		uint32_t set, uint32_t binding, const Buffer& buffer, vk::DeviceSize offset = 0, vk::DeviceSize range = 0);
	void SetVertexAttribute(uint32_t attribute, uint32_t binding, vk::Format format, vk::DeviceSize offset);
	void SetVertexBinding(uint32_t binding,
	                      const Buffer& buffer,
	                      vk::DeviceSize offset,
	                      vk::DeviceSize stride,
	                      vk::VertexInputRate inputRate);

	void BeginRenderPass(const RenderPassInfo& info, vk::SubpassContents contents = vk::SubpassContents::eInline);
	void NextSubpass(vk::SubpassContents contents = vk::SubpassContents::eInline);
	void EndRenderPass();

 private:
	CommandBuffer(
		Device& device, vk::CommandBuffer cmdBuf, CommandBufferType type, uint32_t threadIndex, TracyVkCtx tracingContext);

	void BeginCompute();
	void BeginContext();
	void BeginGraphics();
	void BindPipeline(vk::PipelineBindPoint bindPoint, vk::Pipeline pipeline, uint32_t activeDynamicState);
	Pipeline BuildGraphicsPipeline(bool synchronous);
	void FlushDescriptorSet(uint32_t set);
	void FlushDescriptorSets();
	bool FlushGraphicsPipeline(bool synchronous);
	bool FlushRenderState(bool synchronous);
	void RebindDescriptorSet(uint32_t set);
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
};
}  // namespace Vulkan
}  // namespace Luna
