#pragma once

#include <vk_mem_alloc.h>

#include <Luna/Utility/IntrusivePtr.hpp>
#include <Luna/Utility/ObjectPool.hpp>
#include <Luna/Vulkan/Cookie.hpp>
#include <Luna/Vulkan/Enums.hpp>
#include <Luna/Vulkan/InternalSync.hpp>
#include <array>
#include <set>
#include <string>
#include <vector>
#include <vulkan/vulkan.hpp>

#define LUNA_VULKAN_DEBUG
#define LUNA_VULKAN_MT

namespace Luna {
namespace Vulkan {
// ================================
// ===== Forward Declarations =====
// ================================
class CommandBuffer;
struct CommandBufferDeleter;
class CommandPool;
class Context;
class Cookie;
class Device;
class Fence;
struct FenceDeleter;
class Image;
struct ImageCreateInfo;
struct ImageDeleter;
class ImageView;
struct ImageViewCreateInfo;
struct ImageViewDeleter;
class PerformanceQueryPool;
class QueryPool;
class QueryPoolResult;
struct QueryPoolResultDeleter;
class Semaphore;
struct SemaphoreDeleter;
class WSI;
class WSIPlatform;

#ifdef LUNA_VULKAN_MT
using HandleCounter = MultiThreadCounter;
template <typename T>
using VulkanObjectPool = ThreadSafeObjectPool<T>;
#else
using HandleCounter = SingleThreadCounter;
template <typename T>
using VulkanObjectPool = ObjectPool<T>;
#endif

// ========================
// ===== Handle Types =====
// ========================
using CommandBufferHandle        = IntrusivePtr<CommandBuffer>;
using ContextHandle              = IntrusivePtr<Context>;
using DeviceHandle               = IntrusivePtr<Device>;
using FenceHandle                = IntrusivePtr<Fence>;
using ImageHandle                = IntrusivePtr<Image>;
using ImageViewHandle            = IntrusivePtr<ImageView>;
using PerformanceQueryPoolHandle = IntrusivePtr<PerformanceQueryPool>;
using QueryPoolResultHandle      = IntrusivePtr<QueryPoolResult>;
using SemaphoreHandle            = IntrusivePtr<Semaphore>;

// ===========================
// ===== Data Structures =====
// ===========================
// Contains boolean flags indicating whether or not the corresponding extension is available and enabled.
struct Extensions {
	// Instance Extensions
	bool DebugUtils       = false;
	bool PerformanceQuery = false;
	bool Surface          = false;
#ifdef LUNA_VULKAN_DEBUG
	bool ValidationFeatures = false;
#endif
};

// Contains information about available or enabled device features.
struct DeviceFeatures {
	vk::PhysicalDeviceFeatures Core;
	vk::PhysicalDevicePerformanceQueryFeaturesKHR PerformanceQuery;
	vk::PhysicalDeviceVulkan12Features Vulkan12;
};

// Contains information about device properties.
struct DeviceProperties {
	vk::PhysicalDeviceProperties Core;
	vk::PhysicalDevicePerformanceQueryPropertiesKHR PerformanceQuery;
	vk::PhysicalDeviceVulkan12Properties Vulkan12;
};

// Contains all relevant info for our Vulkan device.
struct DeviceInfo {
	vk::PhysicalDevice PhysicalDevice;

	std::vector<vk::ExtensionProperties> AvailableExtensions;
	DeviceFeatures AvailableFeatures;
	vk::PhysicalDeviceMemoryProperties Memory;
	DeviceProperties Properties;
	std::vector<vk::QueueFamilyProperties> QueueFamilies;

	DeviceFeatures EnabledFeatures;
};

// Contains information describing a staging buffer holding raw image data.
struct ImageInitialBuffer {};

// Contains information describing raw image data to be uploaded to a VkImage.
struct ImageInitialData {
	const void* Data     = nullptr;
	uint32_t RowLength   = 0;
	uint32_t ImageHeight = 0;
};

// Contains information depicting which queues are assigned to which functionality.
struct QueueInfo {
	std::array<uint32_t, QueueTypeCount> Families;
	std::array<uint32_t, QueueTypeCount> Indices;
	std::array<vk::Queue, QueueTypeCount> Queues;

	QueueInfo() {
		std::fill(Families.begin(), Families.end(), VK_QUEUE_FAMILY_IGNORED);
		std::fill(Indices.begin(), Indices.end(), VK_QUEUE_FAMILY_IGNORED);
	}

	bool SameIndex(QueueType a, QueueType b) const {
		return Indices[int(a)] == Indices[int(b)];
	}
	bool SameFamily(QueueType a, QueueType b) const {
		return Families[int(a)] == Families[int(b)];
	}
	bool SameQueue(QueueType a, QueueType b) const {
		return Queues[int(a)] == Queues[int(b)];
	}
	std::vector<uint32_t> UniqueFamilies() const {
		std::set<uint32_t> unique;
		for (const auto& family : Families) {
			if (family != VK_QUEUE_FAMILY_IGNORED) { unique.insert(family); }
		}

		return std::vector<uint32_t>(unique.begin(), unique.end());
	}

	uint32_t& Family(QueueType type) {
		return Families[int(type)];
	}
	const uint32_t& Family(QueueType type) const {
		return Families[int(type)];
	}
	uint32_t& Index(QueueType type) {
		return Indices[int(type)];
	}
	const uint32_t& Index(QueueType type) const {
		return Indices[int(type)];
	}
	vk::Queue& Queue(QueueType type) {
		return Queues[int(type)];
	}
	const vk::Queue& Queue(QueueType type) const {
		return Queues[int(type)];
	}
};

// =============================
// ===== Utility Functions =====
// =============================
// Calculate the number of mip levels required for a full mip chain.
uint32_t CalculateMipLevels(uint32_t width = 1, uint32_t height = 1, uint32_t depth = 1);
uint32_t CalculateMipLevels(vk::Extent2D extent);
uint32_t CalculateMipLevels(vk::Extent3D extent);
// Returns a string formatted with KB/MB/GB size suffixes, whatever is largest.
std::string FormatSize(vk::DeviceSize size);
}  // namespace Vulkan
}  // namespace Luna
