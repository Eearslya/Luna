#pragma once

#include <Luna/Vulkan/Common.hpp>

namespace Luna {
namespace Vulkan {
class Context;

class Device final : NonCopyable {
 public:
	Device(const Context& context);
	~Device() noexcept;

	void WaitIdle();

 private:
	struct SyncInfo {
#ifdef LUNA_VULKAN_MT
		std::mutex Mutex;
		std::condition_variable Condition;
#endif
		uint32_t PendingCommandBuffers = 0;
	};

	void WaitIdleNoLock();

	const ExtensionInfo& _extensions;
	const vk::Instance& _instance;
	const vk::SurfaceKHR& _surface;
	const GPUInfo& _gpuInfo;
	const QueueInfo& _queues;
	const vk::PhysicalDevice& _gpu;
	const vk::Device& _device;

#ifdef LUNA_VULKAN_MT
	std::atomic_uint64_t _nextCookie;
#else
	uint64_t _nextCookie = 0;
#endif
	SyncInfo _sync;
};
}  // namespace Vulkan
}  // namespace Luna
