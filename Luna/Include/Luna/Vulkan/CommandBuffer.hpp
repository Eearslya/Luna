#pragma once

#include <Luna/Vulkan/Common.hpp>

namespace Luna {
namespace Vulkan {
struct PipelineCompileInfo {
	const Program* Program = nullptr;
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
	void SetProgram(const Program* program);

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

	const RenderPass* _actualRenderPass = nullptr;
	CommandBufferDirtyFlags _dirty;
	const Framebuffer* _framebuffer = nullptr;
	vk::Pipeline _pipeline;
	static vk::PipelineLayout _pipelineLayout;
	vk::Rect2D _scissor                     = {{0, 0}, {0, 0}};
	vk::PipelineStageFlags _swapchainStages = {};
	vk::Viewport _viewport                  = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};

	PipelineCompileInfo _pipelineCompileInfo;
};
}  // namespace Vulkan

template <>
struct EnableBitmaskOperators<Vulkan::CommandBufferDirtyFlagBits> : std::true_type {};
}  // namespace Luna
