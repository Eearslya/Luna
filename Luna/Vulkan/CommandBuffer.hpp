#pragma once

#include "Common.hpp"

namespace Luna {
namespace Vulkan {
struct CommandBufferDeleter {
	void operator()(CommandBuffer* commandBuffer);
};

class CommandBuffer : public IntrusivePtrEnabled<CommandBuffer, CommandBufferDeleter, HandleCounter> {
 public:
	friend class ObjectPool<CommandBuffer>;
	friend struct CommandBufferDeleter;

	~CommandBuffer() noexcept;

	vk::CommandBuffer GetCommandBuffer() const {
		return _commandBuffer;
	}
	vk::PipelineStageFlags GetSwapchainStages() const {
		return _swapchainStages;
	}
	CommandBufferType GetType() const {
		return _type;
	}

	void End();

	void Barrier(vk::PipelineStageFlags srcStage,
	             vk::AccessFlags srcAccess,
	             vk::PipelineStageFlags dstStage,
	             vk::AccessFlags dstAccess);
	void Barrier(vk::PipelineStageFlags srcStage,
	             vk::PipelineStageFlags dstStage,
	             const std::vector<vk::MemoryBarrier>& memoryBarriers       = {},
	             const std::vector<vk::BufferMemoryBarrier>& bufferBarriers = {},
	             const std::vector<vk::ImageMemoryBarrier>& imageBarriers   = {});
	void ImageBarrier(const Image& image,
	                  vk::ImageLayout oldLayout,
	                  vk::ImageLayout newLayout,
	                  vk::PipelineStageFlags srcStage,
	                  vk::AccessFlags srcAccess,
	                  vk::PipelineStageFlags dstStage,
	                  vk::AccessFlags dstAccess);
	void MipmapBarrier(const Image& image,
	                   vk::ImageLayout baseLevelLayout,
	                   vk::PipelineStageFlags srcStage,
	                   vk::AccessFlags srcAccess,
	                   bool needTopLevelBarrier = true);

	void BlitImage(const Image& dst,
	               const Image& src,
	               const vk::Offset3D& dstOffset0,
	               const vk::Offset3D& dstExtent,
	               const vk::Offset3D& srcOffset0,
	               const vk::Offset3D& srcExtent,
	               unsigned dstLevel,
	               unsigned srcLevel,
	               unsigned dstBaseLayer = 0,
	               uint32_t srcBaseLayer = 0,
	               unsigned numLayers    = 1,
	               vk::Filter filter     = vk::Filter::eLinear);
	void CopyBuffer(const Buffer& dst, const Buffer& src);
	void CopyBuffer(
		const Buffer& dst, vk::DeviceSize dstOffset, const Buffer& src, vk::DeviceSize srcOffset, vk::DeviceSize size);
	void CopyBufferToImage(const Image& image, const Buffer& buffer, const std::vector<vk::BufferImageCopy>& blits);
	void GenerateMipmaps(const Image& image);

 private:
	CommandBuffer(Device& device, vk::CommandBuffer commandBuffer, CommandBufferType type, uint32_t threadIndex);

	Device& _device;
	vk::CommandBuffer _commandBuffer;
	CommandBufferType _type;
	uint32_t _threadIndex;

	vk::PipelineStageFlags _swapchainStages;
};
}  // namespace Vulkan
}  // namespace Luna
