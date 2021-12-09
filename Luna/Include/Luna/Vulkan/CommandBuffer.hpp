#pragma once

#include <Luna/Vulkan/Common.hpp>

namespace Luna {
namespace Vulkan {
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

 private:
	CommandBuffer(Device& device, vk::CommandBuffer commandBuffer, CommandBufferType type, uint32_t threadIndex);

	Device& _device;
	vk::CommandBuffer _commandBuffer;
	CommandBufferType _commandBufferType;
	uint32_t _threadIndex;
};
}  // namespace Vulkan
}  // namespace Luna
