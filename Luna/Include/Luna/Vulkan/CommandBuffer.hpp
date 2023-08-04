#pragma once

#include <Luna/Vulkan/Common.hpp>

namespace Luna {
namespace Vulkan {
struct CommandBufferDeleter {
	void operator()(CommandBuffer* commandBuffer);
};

class CommandBuffer : public VulkanObject<CommandBuffer, CommandBufferDeleter> {
	friend class ObjectPool<CommandBuffer>;
	friend struct CommandBufferDeleter;

 public:
	~CommandBuffer() noexcept;

	vk::CommandBuffer GetCommandBuffer() const noexcept {
		return _commandBuffer;
	}
	CommandBufferType GetCommandBufferType() const noexcept {
		return _type;
	}
	vk::PipelineStageFlags2 GetSwapchainStages() const noexcept {
		return _swapchainStages;
	}

	void Begin();
	void End();

	void Barrier(const vk::DependencyInfo& dependency);
	void BufferBarrier(const Buffer& buffer,
	                   vk::PipelineStageFlags2 srcStages,
	                   vk::AccessFlags2 srcAccess,
	                   vk::PipelineStageFlags2 dstStages,
	                   vk::AccessFlags2 dstAccess);

	void CopyBuffer(const Buffer& dst, const Buffer& src);
	void CopyBuffer(const Buffer& dst, const Buffer& src, const std::vector<vk::BufferCopy>& copies);
	void CopyBuffer(
		const Buffer& dst, vk::DeviceSize dstOffset, const Buffer& src, vk::DeviceSize srcOffset, vk::DeviceSize size);
	void FillBuffer(const Buffer& dst, uint8_t value);
	void FillBuffer(const Buffer& dst, uint8_t value, vk::DeviceSize offset, vk::DeviceSize size);

 private:
	CommandBuffer(Device& device,
	              CommandBufferType type,
	              vk::CommandBuffer commandBuffer,
	              uint32_t threadIndex,
	              const std::string& debugName);

	Device& _device;
	CommandBufferType _type;
	vk::CommandBuffer _commandBuffer;
	uint32_t _threadIndex;
	std::string _debugName;

	vk::PipelineStageFlags2 _swapchainStages;
};
}  // namespace Vulkan
}  // namespace Luna
