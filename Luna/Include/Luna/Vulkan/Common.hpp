#pragma once

#include <vk_mem_alloc.h>

#include <Luna/Common.hpp>
#include <Luna/Utility/ObjectPool.hpp>
#include <Luna/Vulkan/Cookie.hpp>
#include <Luna/Vulkan/Enums.hpp>
#include <Luna/Vulkan/InternalSync.hpp>
#include <vulkan/vulkan.hpp>

#define LUNA_VULKAN_DEBUG

namespace Luna {
namespace Vulkan {
/* ============================
** ===== Helper Templates =====
*  ============================ */
template <typename T, typename Deleter = std::default_delete<T>>
using VulkanObject = IntrusivePtrEnabled<T, Deleter, MultiThreadCounter>;
template <typename T>
using VulkanObjectPool = ThreadSafeObjectPool<T>;

/* ================================
** ===== Forward Declarations =====
*  ================================ */
class Buffer;
struct BufferCreateInfo;
struct BufferDeleter;
class CommandBuffer;
struct CommandBufferDeleter;
class CommandPool;
class Context;
class Device;
class Fence;
struct FenceDeleter;
class Semaphore;
struct SemaphoreDeleter;

/* ===============================
** ===== Handle Declarations =====
*  =============================== */
using BufferHandle        = IntrusivePtr<Buffer>;
using CommandBufferHandle = IntrusivePtr<CommandBuffer>;
using ContextHandle       = IntrusivePtr<Context>;
using DeviceHandle        = IntrusivePtr<Device>;
using FenceHandle         = IntrusivePtr<Fence>;
using SemaphoreHandle     = IntrusivePtr<Semaphore>;

/* ===========================
** ===== Data Structures =====
*  =========================== */
struct Extensions {
	bool DebugUtils              = false;
	bool GetSurfaceCapabilities2 = false;
	bool Surface                 = false;
	bool SurfaceMaintenance1     = false;
	bool SwapchainColorspace     = false;
#ifdef LUNA_VULKAN_DEBUG
	bool ValidationFeatures = false;
#endif

	bool Maintenance4     = false;
	bool Synchronization2 = false;
};

struct DeviceFeatures {
	vk::PhysicalDeviceFeatures Core;
	vk::PhysicalDeviceSynchronization2Features Synchronization2;
	vk::PhysicalDeviceVulkan12Features Vulkan12;
};

struct DeviceProperties {
	vk::PhysicalDeviceProperties Core;
	vk::PhysicalDeviceVulkan12Properties Vulkan12;
};

struct DeviceInfo {
	vk::PhysicalDevice PhysicalDevice;

	std::vector<vk::ExtensionProperties> AvailableExtensions;
	DeviceFeatures AvailableFeatures;
	vk::PhysicalDeviceMemoryProperties Memory;
	DeviceProperties Properties;
	std::vector<vk::QueueFamilyProperties> QueueFamilies;

	DeviceFeatures EnabledFeatures;
};

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

	uint32_t& Index(QueueType type) {
		return Indices[int(type)];
	}
	const uint32_t& Index(QueueType type) const {
		return Indices[int(type)];
	}
	uint32_t& Family(QueueType type) {
		return Families[int(type)];
	}
	const uint32_t& Family(QueueType type) const {
		return Families[int(type)];
	}
	vk::Queue& Queue(QueueType type) {
		return Queues[int(type)];
	}
	const vk::Queue& Queue(QueueType type) const {
		return Queues[int(type)];
	}
};

struct Size {
	Size() = default;
	Size(vk::DeviceSize value) : Value(value) {}

	vk::DeviceSize Value = 0;
};

struct Version {
	Version() = default;
	Version(uint32_t value) : Value(value) {}

	uint32_t Value = 0;
};

/* ============================
** ===== Helper Functions =====
*  ============================ */
vk::AccessFlags DowngradeAccessFlags2(vk::AccessFlags2 access);
vk::PipelineStageFlags DowngradePipelineStageFlags2(vk::PipelineStageFlags2 stages);
vk::PipelineStageFlags DowngradeDstPipelineStageFlags2(vk::PipelineStageFlags2 stages);
vk::PipelineStageFlags DowngradeSrcPipelineStageFlags2(vk::PipelineStageFlags2 stages);
}  // namespace Vulkan
}  // namespace Luna

template <>
struct fmt::formatter<Luna::Vulkan::Version> : fmt::formatter<std::string> {
	auto format(const Luna::Vulkan::Version& version, format_context& ctx) const -> decltype(ctx.out()) {
		return format_to(ctx.out(),
		                 "{}.{}.{}",
		                 VK_API_VERSION_MAJOR(version.Value),
		                 VK_API_VERSION_MINOR(version.Value),
		                 VK_API_VERSION_PATCH(version.Value));
	}
};

template <>
struct fmt::formatter<Luna::Vulkan::Size> : fmt::formatter<std::string> {
	auto format(const Luna::Vulkan::Size byteSize, format_context& ctx) const -> decltype(ctx.out()) {
		constexpr const vk::DeviceSize kib = 1024;
		constexpr const vk::DeviceSize mib = kib * 1024;
		constexpr const vk::DeviceSize gib = mib * 1024;
		const auto size                    = byteSize.Value;

		if (size >= gib) {
			return format_to(ctx.out(), "{:.2f} GiB", float(size) / gib);
		} else if (size >= mib) {
			return format_to(ctx.out(), "{:.2f} MiB", float(size) / mib);
		} else {
			return format_to(ctx.out(), "{} B", size);
		}
	}
};
