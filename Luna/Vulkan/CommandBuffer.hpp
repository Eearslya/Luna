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

 private:
	CommandBuffer(Device& device, vk::CommandBuffer commandBuffer, CommandBufferType type, uint32_t threadIndex);

	Device& _device;
	vk::CommandBuffer _commandBuffer;
	CommandBufferType _type;
	uint32_t _threadIndex;
};
}  // namespace Vulkan
}  // namespace Luna
