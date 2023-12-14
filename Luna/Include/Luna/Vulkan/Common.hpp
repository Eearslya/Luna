#pragma once

#include <vk_mem_alloc.h>

#include <Luna/Common.hpp>
#include <Luna/Utility/IntrusiveHashMap.hpp>
#include <Luna/Utility/ObjectPool.hpp>
#include <Luna/Utility/TemporaryHashMap.hpp>
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
template <typename T>
using HashedObject = IntrusiveHashMapEnabled<T>;
template <typename T, typename Deleter = std::default_delete<T>>
using VulkanObject = IntrusivePtrEnabled<T, Deleter, MultiThreadCounter>;
template <typename T>
using VulkanCache = ThreadSafeIntrusiveHashMapReadCached<T>;
template <typename T>
using VulkanCacheReadWrite = ThreadSafeIntrusiveHashMap<T>;
template <typename T>
using VulkanObjectPool = ThreadSafeObjectPool<T>;

/* ================================
** ===== Forward Declarations =====
*  ================================ */
class BindlessAllocator;
class BindlessDescriptorPool;
struct BindlessDescriptorPoolDeleter;
class Buffer;
struct BufferCreateInfo;
struct BufferDeleter;
class CommandBuffer;
struct CommandBufferDeleter;
class CommandPool;
class Context;
class DescriptorSetAllocator;
struct DescriptorSetLayout;
class Device;
class Fence;
struct FenceDeleter;
class Framebuffer;
struct FramebufferNode;
class Image;
struct ImageCreateInfo;
struct ImageDeleter;
struct ImageInitialData;
class ImageView;
struct ImageViewCreateInfo;
struct ImageViewDeleter;
class ImmutableSampler;
class PipelineLayout;
class Program;
struct ProgramResourceLayout;
class RenderPass;
struct RenderPassInfo;
class Sampler;
struct SamplerCreateInfo;
struct SamplerDeleter;
class Semaphore;
struct SemaphoreDeleter;
class Shader;
struct ShaderResourceLayout;
struct TransientAttachmentNode;

/* ===============================
** ===== Handle Declarations =====
*  =============================== */
using BufferHandle        = IntrusivePtr<Buffer>;
using CommandBufferHandle = IntrusivePtr<CommandBuffer>;
using ContextHandle       = IntrusivePtr<Context>;
using DeviceHandle        = IntrusivePtr<Device>;
using FenceHandle         = IntrusivePtr<Fence>;
using ImageHandle         = IntrusivePtr<Image>;
using ImageViewHandle     = IntrusivePtr<ImageView>;
using SamplerHandle       = IntrusivePtr<Sampler>;
using SemaphoreHandle     = IntrusivePtr<Semaphore>;

/* ===========================
** ===== Constant Values =====
*  =========================== */
constexpr int DescriptorSetsPerPool    = 16;
constexpr int MaxBindlessDescriptors   = 16384;
constexpr int MaxColorAttachments      = 8;
constexpr int MaxDescriptorBindings    = 32;
constexpr int MaxDescriptorSets        = 4;
constexpr int MaxPushConstantSize      = 128;
constexpr int MaxUniformBufferSize     = 16384;
constexpr int MaxVertexAttributes      = 16;
constexpr int MaxVertexBindings        = 8;
constexpr int MaxUserSpecConstants     = 8;
constexpr int MaxInternalSpecConstants = 4;
constexpr int MaxSpecConstants         = MaxUserSpecConstants + MaxInternalSpecConstants;

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
};

struct DeviceFeatures {
	vk::PhysicalDeviceFeatures Core;
	vk::PhysicalDeviceVulkan12Features Vulkan12;
	vk::PhysicalDeviceVulkan13Features Vulkan13;
};

struct DeviceProperties {
	vk::PhysicalDeviceProperties Core;
	vk::PhysicalDeviceVulkan12Properties Vulkan12;
	vk::PhysicalDeviceVulkan13Properties Vulkan13;
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

struct Pipeline {
	vk::Pipeline Pipeline;
	CommandBufferDirtyFlags DynamicMask;
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

struct ResourceBinding {
	union {
		vk::DescriptorBufferInfo Buffer;
		struct {
			vk::DescriptorImageInfo Float;
			vk::DescriptorImageInfo Integer;
		} Image;
		vk::BufferView BufferView;
	};

	vk::DeviceSize DynamicOffset = 0;
	uint64_t Cookie              = 0;
	uint64_t SecondaryCookie     = 0;
};

struct ResourceBindings {
	ResourceBinding Bindings[MaxDescriptorSets][MaxDescriptorBindings] = {};
	uint8_t PushConstantData[MaxPushConstantSize]                      = {0};
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
inline uint32_t CalculateMipLevels(uint32_t width, uint32_t height, uint32_t depth) {
	return std::floor(std::log2(std::max(std::max(width, height), depth))) + 1;
}

inline uint32_t CalculateMipLevels(vk::Extent2D extent) {
	return CalculateMipLevels(extent.width, extent.height, 1);
}

inline uint32_t CalculateMipLevels(vk::Extent3D extent) {
	return CalculateMipLevels(extent.width, extent.height, extent.depth);
}
}  // namespace Vulkan
}  // namespace Luna

template <>
struct std::formatter<Luna::Vulkan::Version> : std::formatter<std::string> {
	auto format(const Luna::Vulkan::Version& version, format_context& ctx) const -> decltype(ctx.out()) {
		return format_to(ctx.out(),
		                 "{}.{}.{}",
		                 VK_API_VERSION_MAJOR(version.Value),
		                 VK_API_VERSION_MINOR(version.Value),
		                 VK_API_VERSION_PATCH(version.Value));
	}
};

template <>
struct std::formatter<Luna::Vulkan::Size> : std::formatter<std::string> {
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

template <typename T>
concept HasVulkanToString = requires(T t) {
															{ vk::to_string(t) };
														};

template <typename T>
	requires(HasVulkanToString<T>)
struct std::formatter<T> : std::formatter<std::string> {
	auto format(const T value, format_context& ctx) const -> decltype(ctx.out()) {
		return format_to(ctx.out(), "{}", vk::to_string(value));
	}
};

template <>
struct std::formatter<vk::Extent2D> : std::formatter<std::string> {
	auto format(const vk::Extent2D extent, format_context& ctx) const -> decltype(ctx.out()) {
		return format_to(ctx.out(), "{} x {}", extent.width, extent.height);
	}
};

template <>
struct std::formatter<vk::Extent3D> : std::formatter<std::string> {
	auto format(const vk::Extent3D extent, format_context& ctx) const -> decltype(ctx.out()) {
		return format_to(ctx.out(), "{} x {} x {}", extent.width, extent.height, extent.depth);
	}
};
