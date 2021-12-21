#pragma once

#include <Luna/Vulkan/Common.hpp>

namespace Luna {
namespace Vulkan {
struct IndexState {
	vk::Buffer Buffer;
	vk::DeviceSize Offset   = 0;
	vk::IndexType IndexType = vk::IndexType::eUint32;
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
	void EndRenderPass();

	void Draw(uint32_t vertexCount, uint32_t instanceCount = 1, uint32_t firstVertex = 0, uint32_t firstInstance = 0);
	void DrawIndexed(uint32_t indexCount,
	                 uint32_t instanceCount = 1,
	                 uint32_t firstIndex    = 0,
	                 int32_t vertexOffset   = 0,
	                 uint32_t firstInstance = 0);
	void SetIndexBuffer(const Buffer& buffer, vk::DeviceSize offset, vk::IndexType indexType);
	void SetProgram(const Program* program);
	void SetSampler(uint32_t set, uint32_t binding, const Sampler& sampler);
	void SetTexture(uint32_t set, uint32_t binding, const ImageView& view);
	void SetTexture(uint32_t set, uint32_t binding, const ImageView& view, const Sampler& sampler);
	void SetTexture(uint32_t set, uint32_t binding, const ImageView& view, StockSampler sampler);
	void SetVertexAttribute(uint32_t attribute, uint32_t binding, vk::Format format, vk::DeviceSize offset);
	void SetVertexBinding(uint32_t binding,
	                      const Buffer& buffer,
	                      vk::DeviceSize offset,
	                      vk::DeviceSize stride,
	                      vk::VertexInputRate inputRate);

 private:
	CommandBuffer(Device& device, vk::CommandBuffer commandBuffer, CommandBufferType type, uint32_t threadIndex);

	vk::Pipeline BuildGraphicsPipeline(bool synchronous);
	bool FlushGraphicsPipeline(bool synchronous);
	bool FlushRenderState(bool synchronous);
	void SetViewportScissor();

	Device& _device;
	vk::CommandBuffer _commandBuffer;
	CommandBufferType _commandBufferType;
	uint32_t _threadIndex;

	uint32_t _activeVertexBuffers             = 0;
	const RenderPass* _actualRenderPass       = nullptr;
	DescriptorBindingState _descriptorBinding = {};
	CommandBufferDirtyFlags _dirty;
	uint32_t _dirtyDescriptorSets   = 0;
	uint32_t _dirtyVertexBuffers    = 0;
	const Framebuffer* _framebuffer = nullptr;
	IndexState _indexBuffer         = {};
	vk::Pipeline _pipeline;
	vk::PipelineLayout _pipelineLayout;
	PipelineLayout* _programLayout          = nullptr;
	vk::Rect2D _scissor                     = {{0, 0}, {0, 0}};
	vk::PipelineStageFlags _swapchainStages = {};
	VertexBindingState _vertexBindings      = {};
	vk::Viewport _viewport                  = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};

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
