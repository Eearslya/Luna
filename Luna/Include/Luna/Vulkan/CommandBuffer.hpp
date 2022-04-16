#pragma once

#include <Luna/Vulkan/Common.hpp>
#include <TracyVulkan.hpp>

#undef MemoryBarrier

namespace tracy {}

namespace Luna {
namespace Vulkan {
struct CommandTracingState {};

struct IndexState {
	vk::Buffer Buffer;
	vk::DeviceSize Offset   = 0;
	vk::IndexType IndexType = vk::IndexType::eUint32;
};

constexpr static const int BlendFactorBits = 5;
constexpr static const int BlendOpBits     = 3;
constexpr static const int CompareOpBits   = 3;
constexpr static const int CullModeBits    = 2;
constexpr static const int FrontFaceBits   = 1;
constexpr static const int StencilOpBits   = 3;
constexpr static const int TopologyBits    = 4;

union PipelineState {
	struct {
		// Topology
		unsigned PrimitiveRestart : 1;
		unsigned Topology : TopologyBits;
		unsigned Wireframe : 1;

		// Culling
		unsigned CullMode : CullModeBits;
		unsigned FrontFace : FrontFaceBits;

		// Depth
		unsigned DepthBiasEnable : 1;
		unsigned DepthClamp : 1;
		unsigned DepthCompare : CompareOpBits;
		unsigned DepthTest : 1;
		unsigned DepthWrite : 1;

		// Stencil
		unsigned StencilTest : 1;
		unsigned StencilFrontFail : StencilOpBits;
		unsigned StencilFrontPass : StencilOpBits;
		unsigned StencilFrontDepthFail : StencilOpBits;
		unsigned StencilFrontCompareOp : CompareOpBits;
		unsigned StencilBackFail : StencilOpBits;
		unsigned StencilBackPass : StencilOpBits;
		unsigned StencilBackDepthFail : StencilOpBits;
		unsigned StencilBackCompareOp : CompareOpBits;

		// Blending
		unsigned BlendEnable : 1;
		unsigned SrcColorBlend : BlendFactorBits;
		unsigned DstColorBlend : BlendFactorBits;
		unsigned ColorBlendOp : BlendOpBits;
		unsigned SrcAlphaBlend : BlendFactorBits;
		unsigned DstAlphaBlend : BlendFactorBits;
		unsigned AlphaBlendOp : BlendOpBits;

		// Misc
		unsigned AlphaToCoverage : 1;
		unsigned AlphaToOne : 1;
		unsigned SampleShading : 1;
		unsigned ConservativeRaster : 1;

		// Compute
		unsigned SubgroupControlSize : 1;
		unsigned SubgroupFullGroup : 1;
		unsigned SubgroupMinimumSizeLog2 : 3;
		unsigned SubgroupMaximumSizeLog2 : 3;

		// Write Mask
		uint32_t WriteMask;
	};
	uint32_t Data[4];
};

struct VertexAttributeState {
	uint32_t Binding      = 0;
	vk::Format Format     = vk::Format::eUndefined;
	vk::DeviceSize Offset = 0;
};

struct VertexBindingState {
	std::array<vk::Buffer, MaxVertexBuffers> Buffers     = {};
	std::array<vk::DeviceSize, MaxVertexBuffers> Offsets = {};
};

struct PipelineCompileInfo {
	const RenderPass* CompatibleRenderPass                              = nullptr;
	const Program* Program                                              = nullptr;
	PipelineState StaticState                                           = {};
	uint32_t SubpassIndex                                               = 0;
	std::array<VertexAttributeState, MaxVertexBuffers> VertexAttributes = {};
	std::array<vk::VertexInputRate, MaxVertexBuffers> VertexInputRates  = {};
	std::array<vk::DeviceSize, MaxVertexBuffers> VertexStrides          = {};

	Hash CachedHash                      = {};
	mutable uint32_t ActiveVertexBuffers = 0;
	Hash GetHash() const;
};

struct ResourceBinding {
	union {
		vk::DescriptorBufferInfo Buffer;
		struct {
			vk::DescriptorImageInfo Float;
			vk::DescriptorImageInfo Integer;
		} Image;
		vk::BufferView BufferView;
	};
	vk::DeviceSize DynamicOffset = 0;
};

struct DescriptorSetBindings {
	std::array<ResourceBinding, MaxDescriptorBindings> Bindings;
	std::array<uint64_t, MaxDescriptorBindings> Cookies;
	std::array<uint64_t, MaxDescriptorBindings> SecondaryCookies;
};

struct DescriptorBindingState {
	std::array<DescriptorSetBindings, MaxDescriptorSets> Sets;
	uint8_t PushConstantData[MaxPushConstantSize] = {};
};

enum class CommandBufferDirtyFlagBits {
	StaticState      = 1 << 0,
	Pipeline         = 1 << 1,
	Viewport         = 1 << 2,
	Scissor          = 1 << 3,
	DepthBias        = 1 << 4,
	StencilReference = 1 << 5,
	StaticVertex     = 1 << 6,
	PushConstants    = 1 << 7,
	DynamicState     = Viewport | Scissor | DepthBias | StencilReference
};
using CommandBufferDirtyFlags = Bitmask<CommandBufferDirtyFlagBits>;

struct CommandBufferDeleter {
	void operator()(CommandBuffer* buffer);
};

class CommandBuffer : public IntrusivePtrEnabled<CommandBuffer, CommandBufferDeleter, HandleCounter> {
	friend struct CommandBufferDeleter;
	friend class ObjectPool<CommandBuffer>;

 public:
	~CommandBuffer() noexcept;

	vk::CommandBuffer GetCommandBuffer() const {
		return _commandBuffer;
	}
	vk::PipelineStageFlags GetSwapchainStages() const {
		return _swapchainStages;
	}
	uint32_t GetThreadIndex() const {
		return _threadIndex;
	}
	TracyVkCtx GetTracing() const {
		return _tracing;
	}
	CommandBufferType GetType() const {
		return _commandBufferType;
	}

	void Begin();
	void End();

	void Barrier(vk::PipelineStageFlags srcStages,
	             vk::AccessFlags srcAccess,
	             vk::PipelineStageFlags dstStages,
	             vk::AccessFlags dstAccess);
	void Barrier(vk::PipelineStageFlags srcStages,
	             vk::PipelineStageFlags dstStages,
	             const vk::ArrayProxy<const vk::MemoryBarrier>& barriers,
	             const vk::ArrayProxy<const vk::BufferMemoryBarrier>& buffers,
	             const vk::ArrayProxy<const vk::ImageMemoryBarrier>& images);
	void BlitImage(Image& dst,
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
	               vk::Filter filter);
	void CopyBuffer(Buffer& dst, Buffer& src);
	void CopyBuffer(Buffer& dst, vk::DeviceSize dstOffset, Buffer& src, vk::DeviceSize srcOffset, vk::DeviceSize bytes);
	void CopyBufferToImage(Image& dst, Buffer& src, const std::vector<vk::BufferImageCopy>& copies);
	void CopyImage(Image& dst,
	               Image& src,
	               const vk::Offset3D& dstOffset,
	               const vk::Offset3D& srcOffset,
	               const vk::Extent3D& extent,
	               const vk::ImageSubresourceLayers& dstSubresource,
	               const vk::ImageSubresourceLayers& srcSubresource);
	void GenerateMipmaps(Image& image,
	                     vk::ImageLayout baseLayout,
	                     vk::PipelineStageFlags srcStage,
	                     vk::AccessFlags srcAccess,
	                     bool needTopLevelBarrier);
	void ImageBarrier(Image& image,
	                  vk::ImageLayout oldLayout,
	                  vk::ImageLayout newLayout,
	                  vk::PipelineStageFlags srcStages,
	                  vk::AccessFlags srcAccess,
	                  vk::PipelineStageFlags dstStages,
	                  vk::AccessFlags dstAccess);

	void BeginRenderPass(const RenderPassInfo& info);
	void NextSubpass();
	void EndRenderPass();

	void ClearRenderState();
	void SetOpaqueState();
	void SetTransparentSpriteState();
	void SetCullMode(vk::CullModeFlagBits mode);
	void SetDepthClamp(bool clamp);
	void SetDepthCompareOp(vk::CompareOp op);
	void SetDepthWrite(bool write);
	void SetFrontFace(vk::FrontFace front);
	void SetScissor(const vk::Rect2D& scissor);

	void Draw(uint32_t vertexCount, uint32_t instanceCount = 1, uint32_t firstVertex = 0, uint32_t firstInstance = 0);
	void DrawIndexed(uint32_t indexCount,
	                 uint32_t instanceCount = 1,
	                 uint32_t firstIndex    = 0,
	                 int32_t vertexOffset   = 0,
	                 uint32_t firstInstance = 0);
	void PushConstants(const void* data, vk::DeviceSize offset, vk::DeviceSize range);
	void SetIndexBuffer(const Buffer& buffer, vk::DeviceSize offset, vk::IndexType indexType);
	void SetInputAttachments(uint32_t set, uint32_t firstBinding);
	void SetProgram(const Program* program);
	void SetSampler(uint32_t set, uint32_t binding, const Sampler* sampler);
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

	void BeginZone(const std::string& name, const glm::vec3& color = glm::vec3(0.0f, 0.0f, 0.0f));
	void EndZone();
	void Mark(const std::string& name, const glm::vec3& color = glm::vec3(0.0f, 0.0f, 0.0f));

 private:
	CommandBuffer(Device& device, vk::CommandBuffer commandBuffer, CommandBufferType type, uint32_t threadIndex);

	void BeginContext();
	void BeginCompute();
	void BeginGraphics();
	vk::Pipeline BuildGraphicsPipeline(bool synchronous);
	bool FlushGraphicsPipeline(bool synchronous);
	bool FlushRenderState(bool synchronous);
	void SetViewportScissor(const RenderPassInfo& info);

	Device& _device;
	vk::CommandBuffer _commandBuffer;
	CommandBufferType _commandBufferType;
	uint32_t _threadIndex;
	TracyVkCtx _tracing = nullptr;

	uint32_t _activeVertexBuffers             = 0;
	const RenderPass* _actualRenderPass       = nullptr;
	DescriptorBindingState _descriptorBinding = {};
	CommandBufferDirtyFlags _dirty;
	uint32_t _dirtyDescriptorSets                                                 = 0;
	uint32_t _dirtyVertexBuffers                                                  = 0;
	const Framebuffer* _framebuffer                                               = nullptr;
	std::array<const ImageView*, MaxColorAttachments + 1> _framebufferAttachments = {nullptr};
	IndexState _indexBuffer                                                       = {};
	bool _isCompute                                                               = false;
	vk::Pipeline _pipeline;
	vk::PipelineLayout _pipelineLayout;
	PipelineLayout* _programLayout          = nullptr;
	vk::Rect2D _scissor                     = {{0, 0}, {0, 0}};
	vk::PipelineStageFlags _swapchainStages = {};
	VertexBindingState _vertexBindings      = {};
	vk::Viewport _viewport                  = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
	size_t _zoneDepth                       = 0;

	PipelineCompileInfo _pipelineCompileInfo;
};
}  // namespace Vulkan

template <>
struct EnableBitmaskOperators<Vulkan::CommandBufferDirtyFlagBits> : std::true_type {};
}  // namespace Luna

template <>
struct std::hash<Luna::Vulkan::PipelineCompileInfo> {
	size_t operator()(const Luna::Vulkan::PipelineCompileInfo& info) {
		return static_cast<size_t>(info.GetHash());
	}
};

static inline uint32_t TracyColor(const glm::vec3& color) {
	uint32_t uColor = 0x000000;
	uColor |= (static_cast<unsigned int>(color.b * 255.0f) & 0xff) << 0;
	uColor |= (static_cast<unsigned int>(color.g * 255.0f) & 0xff) << 8;
	uColor |= (static_cast<unsigned int>(color.r * 255.0f) & 0xff) << 16;
	return uColor;
}

#ifdef LUNA_VULKAN_TRACING
#	define CbZone(cmd, name)         TracyVkZone(cmd->GetTracing(), cmd->GetCommandBuffer(), name)
#	define CbZoneC(cmd, name, color) TracyVkZone(cmd->GetTracing(), cmd->GetCommandBuffer(), name, color)
#else
#	define CbZone(cmd, name)
#	define CbZoneC(cmd, name, color)
#endif
