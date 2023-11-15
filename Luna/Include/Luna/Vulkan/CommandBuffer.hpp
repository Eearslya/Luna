#pragma once

#include <Luna/Vulkan/Common.hpp>

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
		// Depth Testing (8 bits)
		unsigned DepthBiasEnable : 1;
		unsigned DepthCompare : CompareOpBits;
		unsigned DepthTest : 1;
		unsigned DepthWrite : 1;

		// Stencil Testing (25 bits)
		unsigned StencilBackCompareOp : CompareOpBits;
		unsigned StencilBackDepthFail : StencilOpBits;
		unsigned StencilBackFail : StencilOpBits;
		unsigned StencilBackPass : StencilOpBits;
		unsigned StencilFrontCompareOp : CompareOpBits;
		unsigned StencilFrontDepthFail : StencilOpBits;
		unsigned StencilFrontFail : StencilOpBits;
		unsigned StencilFrontPass : StencilOpBits;
		unsigned StencilTest : 1;

		// Culling (3 bits)
		unsigned CullMode : CullModeBits;
		unsigned FrontFace : FrontFaceBits;

		// Blending (27 bits)
		unsigned AlphaBlendOp : BlendOpBits;
		unsigned BlendEnable : 1;
		unsigned ColorBlendOp : BlendOpBits;
		unsigned DstAlphaBlend : BlendFactorBits;
		unsigned DstColorBlend : BlendFactorBits;
		unsigned SrcAlphaBlend : BlendFactorBits;
		unsigned SrcColorBlend : BlendFactorBits;

		// Sample Shading (4 bits)
		unsigned AlphaToCoverage : 1;
		unsigned AlphaToOne : 1;
		unsigned ConservativeRaster : 1;
		unsigned SampleShading : 1;

		// Topology (6 bits)
		unsigned PrimitiveRestart : 1;
		unsigned Topology : 4;
		unsigned Wireframe : 1;

		// Subgroups (8 bits)
		unsigned SubgroupControlSize : 1;
		unsigned SubgroupFullGroup : 1;
		unsigned SubgroupMinimumSizeLog2 : 3;
		unsigned SubgroupMaximumSizeLog2 : 3;

		// Write Mask (32 bits)
		uint32_t WriteMask;
	};  // (113 bits)

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
	std::vector<Program*> ProgramGroup     = {};
	Program* Program                       = nullptr;
	const PipelineLayout* PipelineLayout   = nullptr;
	const RenderPass* CompatibleRenderPass = nullptr;

	PipelineState StaticState           = {};
	PotentialState PotentialStaticState = {};

	std::array<VertexAttributeState, MaxVertexAttributes> Attributes = {};
	std::array<vk::VertexInputRate, MaxVertexBindings> InputRates    = {vk::VertexInputRate::eVertex};
	std::array<vk::DeviceSize, MaxVertexBindings> Strides            = {0};

	uint32_t SubpassIndex           = 0;
	vk::PipelineCache PipelineCache = VK_NULL_HANDLE;
	uint32_t SubgroupSizeTag        = 0;

	mutable Hash CachedHash = 0;
	Hash GetComputeHash() const;
	Hash GetHash(uint32_t& activeVBOs) const;
};

struct CommandBufferDeleter {
	void operator()(CommandBuffer* commandBuffer);
};

class CommandBuffer : public VulkanObject<CommandBuffer, CommandBufferDeleter> {
	friend class ObjectPool<CommandBuffer>;
	friend struct CommandBufferDeleter;

 public:
	~CommandBuffer() noexcept;

	[[nodiscard]] vk::CommandBuffer GetCommandBuffer() const noexcept {
		return _commandBuffer;
	}
	[[nodiscard]] CommandBufferType GetCommandBufferType() const noexcept {
		return _type;
	}
	[[nodiscard]] vk::PipelineStageFlags2 GetSwapchainStages() const noexcept {
		return _swapchainStages;
	}

	// Basic control
	void Begin();
	void End();

	// Pipeline barriers
	void Barrier(const vk::DependencyInfo& dependency);
	void BufferBarrier(const Buffer& buffer,
	                   vk::PipelineStageFlags2 srcStages,
	                   vk::AccessFlags2 srcAccess,
	                   vk::PipelineStageFlags2 dstStages,
	                   vk::AccessFlags2 dstAccess);

	// Buffer transfers
	void CopyBuffer(const Buffer& dst, const Buffer& src);
	void CopyBuffer(const Buffer& dst, const Buffer& src, const std::vector<vk::BufferCopy>& copies);
	void CopyBuffer(
		const Buffer& dst, vk::DeviceSize dstOffset, const Buffer& src, vk::DeviceSize srcOffset, vk::DeviceSize size);
	void FillBuffer(const Buffer& dst, uint8_t value);
	void FillBuffer(const Buffer& dst, uint8_t value, vk::DeviceSize offset, vk::DeviceSize size);

	// Descriptors
	template <typename T>
	void PushConstants(const T& data, vk::DeviceSize offset = 0) {
		PushConstants(sizeof(data), &data, offset);
	}
	void PushConstants(size_t size, const void* data, vk::DeviceSize offset = 0);
	void SetStorageBuffer(uint32_t set, uint32_t binding, const Buffer& buffer);
	void SetStorageBuffer(
		uint32_t set, uint32_t binding, const Buffer& buffer, vk::DeviceSize offset, vk::DeviceSize range);

	// Dispatch and Draw
	void Dispatch(uint32_t groupsX, uint32_t groupsY, uint32_t groupsZ);

	// Render Pass control
	void BeginRenderPass(const RenderPassInfo& rpInfo, vk::SubpassContents contents = vk::SubpassContents::eInline);
	void NextSubpass(vk::SubpassContents contents = vk::SubpassContents::eInline);
	void EndRenderPass();

	// State control
	void SetOpaqueState();
	void SetProgram(Program* program);

 private:
	CommandBuffer(Device& device,
	              CommandBufferType type,
	              vk::CommandBuffer commandBuffer,
	              uint32_t threadIndex,
	              const std::string& debugName);

	void BindPipeline(vk::PipelineBindPoint bindPoint, vk::Pipeline pipeline, CommandBufferDirtyFlags activeDynamicState);
	Pipeline BuildComputePipeline(bool synchronous);
	void BeginCompute();
	void BeginContext();
	void BeginGraphics();
	void ClearRenderState();
	bool FlushComputePipeline(bool synchronous);
	vk::Pipeline FlushComputeState(bool synchronous);
	void FlushDescriptorSet(uint32_t set);
	void FlushDescriptorSets();
	void RebindDescriptorSet(uint32_t set);
	void SetViewportScissor(const RenderPassInfo& rpInfo, const Framebuffer* framebuffer);

	// ----- Core command buffer information -----
	Device& _device;
	CommandBufferType _type;
	vk::CommandBuffer _commandBuffer;
	uint32_t _threadIndex;
	std::string _debugName;

	// ----- Descriptors State -----
	std::array<vk::DescriptorSet, MaxDescriptorSets> _allocatedSets = {};
	// - Currently bound descriptors and push constants.
	ResourceBindings _resources = {};

	// ----- Program State -----
	// - The currently bound Pipeline.
	Pipeline _currentPipeline = {};
	// - Whether or not the current Pipeline is Compute.
	bool _isCompute = true;
	// - The VkPipelineLayout of the currently bound Pipeline.
	vk::PipelineLayout _pipelineLayout;
	// - Includes all state necessary to compile a Pipeline once a draw or dispatch is recorded.
	DeferredPipelineCompile _pipelineState = {};

	// ----- Render Pass State -----
	// - All members are NULL until a graphics render pass has begun, and return to NULL when the render pass ends.
	// - The currently active Render Pass, as per vkCmdBeginRenderPass.
	const RenderPass* _actualRenderPass = nullptr;
	// - The currently set SubpassContents for the active subpass.
	vk::SubpassContents _currentContents = vk::SubpassContents::eInline;
	// - The currently bound Framebuffer, as per VkRenderPassBeginInfo.
	const Framebuffer* _framebuffer = nullptr;
	// - The attachments that make up the currently bound Framebuffer.
	std::array<const ImageView*, MaxColorAttachments + 1> _framebufferAttachments = {nullptr};

	// ----- Vertex Input State -----
	// - Index Buffer State.
	IndexState _indexState = {};
	// - Vertex Buffer(s) State.
	VertexBindingState _vertexBindings = {};

	uint32_t _activeVBOs           = 0;
	CommandBufferDirtyFlags _dirty = ~0u;
	uint32_t _dirtySets            = 0;
	uint32_t _dirtySetsDynamic     = 0;
	uint32_t _dirtyVBOs            = 0;
	DynamicState _dynamicState     = {};
	vk::Rect2D _scissor            = {};
	vk::PipelineStageFlags2 _swapchainStages;
	vk::Viewport _viewport = {};
};
}  // namespace Vulkan
}  // namespace Luna
