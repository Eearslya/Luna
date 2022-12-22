#pragma once

#include <Luna/Vulkan/Common.hpp>
#include <Luna/Vulkan/RenderPass.hpp>

namespace Luna {
namespace Vulkan {
union PipelineState {
	struct {
		uint32_t DepthWrite : 1;
		uint32_t DepthTest : 1;
		uint32_t BlendEnable : 1;

		uint32_t CullMode : 2;
		uint32_t FrontFace : 1;
		uint32_t DepthBiasEnable : 1;

		uint32_t DepthCompare : 3;

		uint32_t StencilTest : 1;
		uint32_t StencilFrontFail : 3;
		uint32_t StencilFrontPass : 3;
		uint32_t StencilFrontDepthFail : 3;
		uint32_t StencilFrontCompareOp : 3;
		uint32_t StencilBackFail : 3;
		uint32_t StencilBackPass : 3;
		uint32_t StencilBackDepthFail : 3;
		uint32_t StencilBackCompareOp : 3;

		uint32_t AlphaToCoverage : 1;
		uint32_t AlphaToOne : 1;
		uint32_t SampleShading : 1;

		uint32_t SrcColorBlend : 5;
		uint32_t DstColorBlend : 5;
		uint32_t ColorBlendOp : 3;
		uint32_t SrcAlphaBlend : 5;
		uint32_t DstAlphaBlend : 5;
		uint32_t AlphaBlendOp : 3;
		uint32_t PrimitiveRestart : 1;
		uint32_t Topology : 4;

		uint32_t Wireframe : 1;
		uint32_t SubroupControlSize : 1;
		uint32_t SubgroupFullGroup : 1;
		uint32_t SubgroupMinimumSizeLog2 : 3;
		uint32_t SubgroupMaximumSizeLog2 : 3;
		uint32_t ConservativeRaster : 1;

		uint32_t WriteMask;
	} State;
	uint32_t Words[4] = {0};
};

struct PotentialState {
	float BlendConstants[4]                  = {0.0f};
	uint32_t SpecConstants[MaxSpecConstants] = {0};
	uint8_t SpecConstantMask                 = 0;
	uint8_t InternalSpecConstantMask         = 0;
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
	vk::Buffer Buffers[MaxVertexBindings];
	vk::DeviceSize Offsets[MaxVertexBindings];
};

struct DeferredPipelineCompile {
	Program* Program                                           = nullptr;
	const RenderPass* CompatibleRenderPass                     = nullptr;
	PipelineState StaticState                                  = {};
	PotentialState PotentialStaticState                        = {};
	VertexAttributeState VertexAttributes[MaxVertexAttributes] = {};
	vk::DeviceSize VertexBindingStrides[MaxVertexBindings]     = {};
	vk::VertexInputRate VertexInputRates[MaxVertexBindings]    = {};
	uint32_t SubpassIndex                                      = 0;
	uint32_t SubgroupSizeTag                                   = 0;

	Hash Hash = 0;
};

struct CommandBufferDeleter {
	void operator()(CommandBuffer* cmdBuf);
};

class CommandBuffer : public IntrusivePtrEnabled<CommandBuffer, CommandBufferDeleter, HandleCounter> {
	friend class ObjectPool<CommandBuffer>;
	friend struct CommandBufferDeleter;

 public:
	~CommandBuffer() noexcept;

	vk::CommandBuffer GetCommandBuffer() const {
		return _commandBuffer;
	}
	vk::PipelineStageFlags GetSwapchainStages() const {
		return _swapchainStages;
	}
	CommandBufferType GetType() const {
		return _commandBufferType;
	}

	void End();

	void Barrier(vk::PipelineStageFlags srcStages,
	             vk::AccessFlags srcAccess,
	             vk::PipelineStageFlags dstStages,
	             vk::AccessFlags dstAccess);
	void Barrier(vk::PipelineStageFlags srcStages,
	             vk::PipelineStageFlags dstStages,
	             const std::vector<vk::MemoryBarrier>& memoryBarriers,
	             const std::vector<vk::BufferMemoryBarrier>& bufferBarriers,
	             const std::vector<vk::ImageMemoryBarrier>& imageBarriers);
	void BarrierPrepareGenerateMipmaps(const Image& image,
	                                   vk::ImageLayout baseLevelLayout,
	                                   vk::PipelineStageFlags srcStages,
	                                   vk::AccessFlags srcAccess,
	                                   bool needTopLevelBarrier = true);
	void BufferBarrier(const Buffer& buffer,
	                   vk::PipelineStageFlags srcStages,
	                   vk::AccessFlags srcAccess,
	                   vk::PipelineStageFlags dstStages,
	                   vk::AccessFlags dstAccess);
	void BufferBarriers(vk::PipelineStageFlags srcStages,
	                    vk::PipelineStageFlags dstStages,
	                    const std::vector<vk::BufferMemoryBarrier>& bufferBarriers);
	void FullBarrier();
	void ImageBarrier(const Image& image,
	                  vk::ImageLayout oldLayout,
	                  vk::ImageLayout newLayout,
	                  vk::PipelineStageFlags srcStages,
	                  vk::AccessFlags srcAccess,
	                  vk::PipelineStageFlags dstStages,
	                  vk::AccessFlags dstAccess);
	void ImageBarriers(vk::PipelineStageFlags srcStages,
	                   vk::PipelineStageFlags dstStages,
	                   const std::vector<vk::ImageMemoryBarrier>& imageBarriers);
	void PixelBarrier();

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
	void FillBuffer(const Buffer& dst, uint8_t value);
	void FillBuffer(const Buffer& dst, uint8_t value, vk::DeviceSize offset, vk::DeviceSize size);
	void GenerateMipmaps(const Image& image);

	void BeginCompute();
	void BeginGraphics();
	void SetOpaqueState();

	void BeginRenderPass(const RenderPassInfo& info, vk::SubpassContents contents = vk::SubpassContents::eInline);
	void NextSubpass(vk::SubpassContents contents = vk::SubpassContents::eInline);
	void EndRenderPass();

 private:
	CommandBuffer(Device& device, vk::CommandBuffer cmdBuf, CommandBufferType type, uint32_t threadIndex);

	void BeginContext();
	void ClearRenderState();
	void SetViewportScissor(const RenderPassInfo& info, const Framebuffer* framebuffer);

	Device& _device;
	vk::CommandBuffer _commandBuffer;
	CommandBufferType _commandBufferType;
	uint32_t _threadIndex;

	const RenderPass* _actualRenderPass                                           = nullptr;
	vk::SubpassContents _currentContents                                          = {};
	CommandBufferDirtyFlags _dirty                                                = ~0u;
	uint32_t _dirtySetMask                                                        = 0;
	uint32_t _dirtySetDynamicMask                                                 = 0;
	DynamicState _dynamicState                                                    = {};
	const Framebuffer* _framebuffer                                               = nullptr;
	std::array<const ImageView*, MaxColorAttachments + 1> _framebufferAttachments = {};
	bool _isCompute                                                               = true;
	DeferredPipelineCompile _pipelineState                                        = {};
	vk::Rect2D _scissor                                                           = {};
	vk::PipelineStageFlags _swapchainStages                                       = {};
	vk::Viewport _viewport                                                        = {};
};
}  // namespace Vulkan
}  // namespace Luna
