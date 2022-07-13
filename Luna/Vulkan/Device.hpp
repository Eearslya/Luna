#pragma once

#include <vk_mem_alloc.h>

#include "Common.hpp"

namespace Luna {
namespace Vulkan {
class Device : public IntrusivePtrEnabled<Device, std::default_delete<Device>, HandleCounter> {
 public:
	Device(const Context& context);
	Device(const Device&)            = delete;
	Device& operator=(const Device&) = delete;
	~Device() noexcept;

	uint64_t AllocateCookie();

 private:
	const ExtensionInfo _extensions;
	const vk::Instance _instance;
	const GPUInfo _gpuInfo;
	const QueueInfo _queues;
	const vk::PhysicalDevice _gpu;
	const vk::Device _device;

	VmaAllocator _allocator;

#ifdef LUNA_VULKAN_MT
	std::atomic_uint64_t _cookie;
#else
	uint64_t _cookie = 0;
#endif
};
}  // namespace Vulkan
}  // namespace Luna
