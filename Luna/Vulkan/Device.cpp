#include "Device.hpp"

#include "Context.hpp"
#include "Utility/Log.hpp"

namespace Luna {
namespace Vulkan {
Device::Device(const Context& context)
		: _extensions(context.GetExtensionInfo()),
			_instance(context.GetInstance()),
			_gpuInfo(context.GetGPUInfo()),
			_queues(context.GetQueueInfo()),
			_gpu(context.GetGPU()),
			_device(context.GetDevice()) {
#ifdef LUNA_VULKAN_MT
	_cookie.store(0);
#endif

	// Initialize our VMA allocator.
	{
#define FN(name) .name = VULKAN_HPP_DEFAULT_DISPATCHER.name
		VmaVulkanFunctions vmaFunctions = {FN(vkGetInstanceProcAddr),
		                                   FN(vkGetDeviceProcAddr),
		                                   FN(vkGetPhysicalDeviceProperties),
		                                   FN(vkGetPhysicalDeviceMemoryProperties),
		                                   FN(vkAllocateMemory),
		                                   FN(vkFreeMemory),
		                                   FN(vkMapMemory),
		                                   FN(vkUnmapMemory),
		                                   FN(vkFlushMappedMemoryRanges),
		                                   FN(vkInvalidateMappedMemoryRanges),
		                                   FN(vkBindBufferMemory),
		                                   FN(vkBindImageMemory),
		                                   FN(vkGetBufferMemoryRequirements),
		                                   FN(vkGetImageMemoryRequirements),
		                                   FN(vkCreateBuffer),
		                                   FN(vkDestroyBuffer),
		                                   FN(vkCreateImage),
		                                   FN(vkDestroyImage),
		                                   FN(vkCmdCopyBuffer)};
#undef FN
#define FN(core) vmaFunctions.core##KHR = reinterpret_cast<PFN_##core##KHR>(VULKAN_HPP_DEFAULT_DISPATCHER.core)
		FN(vkGetBufferMemoryRequirements2);
		FN(vkGetImageMemoryRequirements2);
		FN(vkBindBufferMemory2);
		FN(vkBindImageMemory2);
		FN(vkGetPhysicalDeviceMemoryProperties2);
#undef FN
		if (_extensions.Maintenance4) {
			vmaFunctions.vkGetDeviceBufferMemoryRequirements =
				VULKAN_HPP_DEFAULT_DISPATCHER.vkGetDeviceBufferMemoryRequirementsKHR;
			vmaFunctions.vkGetDeviceImageMemoryRequirements =
				VULKAN_HPP_DEFAULT_DISPATCHER.vkGetDeviceImageMemoryRequirementsKHR;
		}

		const VmaAllocatorCreateInfo allocatorCI = {.physicalDevice   = _gpu,
		                                            .device           = _device,
		                                            .pVulkanFunctions = &vmaFunctions,
		                                            .instance         = _instance,
		                                            .vulkanApiVersion = VK_API_VERSION_1_1};
		const auto allocatorResult               = vmaCreateAllocator(&allocatorCI, &_allocator);
		if (allocatorResult != VK_SUCCESS) {
			throw std::runtime_error("[Vulkan::Device] Failed to create memory allocator!");
		}
	}
}

Device::~Device() noexcept {
	vmaDestroyAllocator(_allocator);
}

uint64_t Device::AllocateCookie() {
#ifdef LUNA_VULKAN_MT
	return _cookie.fetch_add(16, std::memory_order_relaxed) + 16;
#else
	_cookie += 16;
	return _cookie;
#endif
}
}  // namespace Vulkan
}  // namespace Luna
