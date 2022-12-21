#pragma once

#include <Luna/Vulkan/Common.hpp>

namespace Luna {
namespace Vulkan {
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

	void CopyBuffer(const Buffer& dst, const Buffer& src);
	void CopyBuffer(
		const Buffer& dst, vk::DeviceSize dstOffset, const Buffer& src, vk::DeviceSize srcOffset, vk::DeviceSize size);
	void CopyBuffer(const Buffer& dst, const Buffer& src, const std::vector<vk::BufferCopy>& copies);
	void FillBuffer(const Buffer& dst, uint8_t value);
	void FillBuffer(const Buffer& dst, uint8_t value, vk::DeviceSize offset, vk::DeviceSize size);

 private:
	CommandBuffer(Device& device, vk::CommandBuffer cmdBuf, CommandBufferType type, uint32_t threadIndex);

	Device& _device;
	vk::CommandBuffer _commandBuffer;
	CommandBufferType _commandBufferType;
	uint32_t _threadIndex;

	vk::PipelineStageFlags _swapchainStages;
};
}  // namespace Vulkan
}  // namespace Luna
