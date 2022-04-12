#pragma once

#include <Luna/Vulkan/Common.hpp>

namespace Luna {
namespace Vulkan {
class CommandPool : NonCopyable {
 public:
	CommandPool(Device& device, uint32_t familyIndex, bool resettable = false);
	~CommandPool() noexcept;

	vk::CommandBuffer RequestCommandBuffer();
	void Reset();
	void Trim();

 private:
	Device& _device;
	vk::CommandPool _pool;
	std::vector<vk::CommandBuffer> _commandBuffers;
	uint32_t _bufferIndex = 0;
};
}  // namespace Vulkan
}  // namespace Luna
