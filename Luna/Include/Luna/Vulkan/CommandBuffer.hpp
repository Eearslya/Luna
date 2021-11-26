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

 private:
	CommandBuffer(Device& device, vk::CommandBuffer commandBuffer, CommandBufferType type, uint32_t threadIndex);

	Device& _device;
	vk::CommandBuffer _commandBuffer;
	CommandBufferType _commandBufferType;
	uint32_t _threadIndex;
};
}  // namespace Vulkan
}  // namespace Luna
