#pragma once

#include <Luna/Vulkan/Common.hpp>

namespace Luna {
namespace Vulkan {
class Context;

class Device final : NonCopyable {
 public:
	Device(const Context& context);
	~Device() noexcept;

	// General functionality.
	void WaitIdle();

 private:
	// General functionality.
	void WaitIdleNoLock();

	// All of our Vulkan information/objects inherited from Context.
	const ExtensionInfo& _extensions;
	const vk::Instance& _instance;
	const vk::SurfaceKHR& _surface;
	const GPUInfo& _gpuInfo;
	const QueueInfo& _queues;
	const vk::PhysicalDevice& _gpu;
	const vk::Device& _device;

	// Multithreading synchronization objects.
#ifdef LUNA_VULKAN_MT
	std::mutex _mutex;
	std::condition_variable _pendingCommandBuffersCondition;
#endif
	uint32_t _pendingCommandBuffers = 0;
};
}  // namespace Vulkan
}  // namespace Luna
