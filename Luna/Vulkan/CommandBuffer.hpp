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

	void CopyBuffer(const Buffer& dst, const Buffer& src);
	void CopyBuffer(
		const Buffer& dst, vk::DeviceSize dstOffset, const Buffer& src, vk::DeviceSize srcOffset, vk::DeviceSize size);

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
