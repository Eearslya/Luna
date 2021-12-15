#pragma once

#include <vk_mem_alloc.h>

#include <Luna/Utility/Badge.hpp>
#include <Luna/Utility/EnumClass.hpp>
#include <Luna/Utility/IntrusiveHashMap.hpp>
#include <Luna/Utility/IntrusivePtr.hpp>
#include <Luna/Utility/NonCopyable.hpp>
#include <Luna/Utility/ObjectPool.hpp>
#include <Luna/Utility/TemporaryHashMap.hpp>
#include <Luna/Vulkan/Cookie.hpp>
#include <array>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <set>
#include <unordered_map>
#include <vector>
#include <vulkan/vulkan.hpp>

#define LUNA_VULKAN_MT

namespace Luna {
namespace Vulkan {
// Forward declarations.
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
class Framebuffer;
class FramebufferAllocator;
class Image;
struct ImageCreateInfo;
struct ImageDeleter;
class ImageView;
struct ImageViewCreateInfo;
struct ImageViewDeleter;
class RenderPass;
struct RenderPassInfo;
class Sampler;
struct SamplerCreateInfo;
struct SamplerDeleter;
class Semaphore;
struct SemaphoreDeleter;
class Swapchain;

// Handle declarations.
using BufferHandle        = IntrusivePtr<Buffer>;
using CommandBufferHandle = IntrusivePtr<CommandBuffer>;
using FenceHandle         = IntrusivePtr<Fence>;
using ImageHandle         = IntrusivePtr<Image>;
using ImageViewHandle     = IntrusivePtr<ImageView>;
using SamplerHandle       = IntrusivePtr<Sampler>;
using SemaphoreHandle     = IntrusivePtr<Semaphore>;

// Typedefs.
template <typename T>
using HashedObject = IntrusiveHashMapEnabled<T>;

#ifdef LUNA_VULKAN_MT
using HandleCounter = MultiThreadCounter;
template <typename T>
using VulkanCache = ThreadSafeIntrusiveHashMapReadCached<T>;
template <typename T>
using VulkanCacheReadWrite = ThreadSafeIntrusiveHashMap<T>;
template <typename T>
using VulkanObjectPool = ThreadSafeObjectPool<T>;
#else
using HandleCounter = SingleThreadCounter;
template <typename T>
using VulkanCache = IntrusiveHashMap<T>;
template <typename T>
using VulkanCacheReadWrite = IntrusiveHashMap<T>;
template <typename T>
using VulkanObjectPool = ObjectPool<T>;
#endif

// Enums and constants.
constexpr static const int MaxColorAttachments = 8;

template <typename T>
static const char* VulkanEnumToString(const T value) {
	return nullptr;
}

enum class QueueType { Graphics, Transfer, Compute };
constexpr static const int QueueTypeCount = 3;
template <>
const char* VulkanEnumToString<QueueType>(const QueueType value) {
	switch (value) {
		case QueueType::Graphics:
			return "Graphics";
		case QueueType::Transfer:
			return "Transfer";
		case QueueType::Compute:
			return "Compute";
	}

	return "Unknown";
}

enum class CommandBufferType {
	Generic       = static_cast<int>(QueueType::Graphics),
	AsyncTransfer = static_cast<int>(QueueType::Transfer),
	AsyncCompute  = static_cast<int>(QueueType::Compute),
	AsyncGraphics = QueueTypeCount
};
constexpr static const int CommandBufferTypeCount = 4;
template <>
const char* VulkanEnumToString<CommandBufferType>(const CommandBufferType value) {
	switch (value) {
		case CommandBufferType::Generic:
			return "Generic";
		case CommandBufferType::AsyncCompute:
			return "AsyncCompute";
		case CommandBufferType::AsyncTransfer:
			return "AsyncTransfer";
		case CommandBufferType::AsyncGraphics:
			return "AsyncGraphics";
	}

	return "Unknown";
}

enum class FormatCompressionType { Uncompressed, BC, ETC, ASTC };
constexpr static const int FormatCompressionTypeCount = 4;
template <>
const char* VulkanEnumToString<FormatCompressionType>(const FormatCompressionType value) {
	switch (value) {
		case FormatCompressionType::Uncompressed:
			return "Uncompressed";
		case FormatCompressionType::BC:
			return "BC";
		case FormatCompressionType::ETC:
			return "ETC";
		case FormatCompressionType::ASTC:
			return "ASTC";
	}

	return "Unknown";
}

enum class ImageLayoutType { Optimal, General };
constexpr static const int ImageLayoutTypeCount = 2;
template <>
const char* VulkanEnumToString<ImageLayoutType>(const ImageLayoutType value) {
	switch (value) {
		case ImageLayoutType::Optimal:
			return "Optimal";
		case ImageLayoutType::General:
			return "General";
	}

	return "Unknown";
}

enum class StockRenderPass { ColorOnly, Depth, DepthStencil };
constexpr static const int StockRenderPassCount = 3;
template <>
const char* VulkanEnumToString<StockRenderPass>(const StockRenderPass value) {
	switch (value) {
		case StockRenderPass::ColorOnly:
			return "ColorOnly";
		case StockRenderPass::Depth:
			return "Depth";
		case StockRenderPass::DepthStencil:
			return "DepthStencil";
	}

	return "Unknown";
}

// Structures
struct ExtensionInfo {
	bool DebugUtils                   = false;
	bool GetPhysicalDeviceProperties2 = false;
	bool GetSurfaceCapabilities2      = false;
	bool Maintenance1                 = false;
	bool Synchronization2             = false;
	bool TimelineSemaphore            = false;
	bool ValidationFeatures           = false;
};
struct GPUFeatures {
	vk::PhysicalDeviceFeatures Features;
#ifdef VK_ENABLE_BETA_EXTENSIONS
	vk::PhysicalDevicePortabilitySubsetFeaturesKHR PortabilitySubset;
#endif
	vk::PhysicalDeviceSynchronization2FeaturesKHR Synchronization2;
	vk::PhysicalDeviceTimelineSemaphoreFeatures TimelineSemaphore;
};
struct GPUProperties {
	vk::PhysicalDeviceProperties Properties;
	vk::PhysicalDeviceDriverProperties Driver;
#ifdef VK_ENABLE_BETA_EXTENSIONS
	vk::PhysicalDevicePortabilitySubsetPropertiesKHR PortabilitySubset;
#endif
	vk::PhysicalDeviceTimelineSemaphoreProperties TimelineSemaphore;
};
struct GPUInfo {
	std::vector<vk::ExtensionProperties> AvailableExtensions;
	GPUFeatures AvailableFeatures = {};
	std::vector<vk::LayerProperties> Layers;
	vk::PhysicalDeviceMemoryProperties Memory;
	GPUProperties Properties = {};
	std::vector<vk::QueueFamilyProperties> QueueFamilies;

	GPUFeatures EnabledFeatures = {};
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
		return Indices[static_cast<int>(a)] == Indices[static_cast<int>(a)];
	}
	bool SameFamily(QueueType a, QueueType b) const {
		return Families[static_cast<int>(a)] == Families[static_cast<int>(a)];
	}
	bool SameQueue(QueueType a, QueueType b) const {
		return Queues[static_cast<int>(a)] == Queues[static_cast<int>(a)];
	}
	std::vector<uint32_t> UniqueFamilies() const {
		std::set<uint32_t> unique;
		for (const auto& family : Families) {
			if (family != VK_QUEUE_FAMILY_IGNORED) { unique.insert(family); }
		}

		return std::vector<uint32_t>(unique.begin(), unique.end());
	}

	uint32_t& Family(QueueType type) {
		return Families[static_cast<int>(type)];
	}
	const uint32_t& Family(QueueType type) const {
		return Families[static_cast<int>(type)];
	}
	uint32_t& Index(QueueType type) {
		return Indices[static_cast<int>(type)];
	}
	const uint32_t& Index(QueueType type) const {
		return Indices[static_cast<int>(type)];
	}
	vk::Queue& Queue(QueueType type) {
		return Queues[static_cast<int>(type)];
	}
	const vk::Queue& Queue(QueueType type) const {
		return Queues[static_cast<int>(type)];
	}
};

// Simple Helper Functions
inline std::string FormatSize(vk::DeviceSize size) {
	std::ostringstream oss;
	if (size < 1024) {
		oss << size << " B";
	} else if (size < 1024 * 1024) {
		oss << size / 1024.f << " KB";
	} else if (size < 1024 * 1024 * 1024) {
		oss << size / (1024.0f * 1024.0f) << " MB";
	} else {
		oss << size / (1024.0f * 1024.0f * 1024.0f) << " GB";
	}

	return oss.str();
}
}  // namespace Vulkan
}  // namespace Luna
