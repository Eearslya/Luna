#pragma once

#include <Luna/Vulkan/Common.hpp>

namespace Luna {
namespace Vulkan {
class CommandPool {
 public:
	CommandPool(Device& device, uint32_t familyIndex, const std::string& debugName = "");
	CommandPool(const CommandPool&) = delete;
	CommandPool(CommandPool&& other) noexcept;
	CommandPool& operator=(const CommandPool&) = delete;
	CommandPool& operator=(CommandPool&& other) noexcept;
	~CommandPool() noexcept;

	void Begin();
	vk::CommandBuffer RequestCommandBuffer();
	void Trim();

 private:
	Device& _device;
	vk::CommandPool _commandPool;
	std::string _debugName;
	std::vector<vk::CommandBuffer> _commandBuffers;
	uint32_t _commandBufferIndex = 0;
};
}  // namespace Vulkan
}  // namespace Luna
