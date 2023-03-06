#pragma once

#include <vk_mem_alloc.h>

#include <Luna/Utility/IntrusiveHashMap.hpp>
#include <Luna/Utility/IntrusivePtr.hpp>
#include <Luna/Utility/ObjectPool.hpp>
#include <Luna/Utility/TemporaryHashMap.hpp>
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
class BindlessDescriptorPool;
class Buffer;
struct BufferCreateInfo;
class CommandBuffer;
class CommandPool;
class Context;
class DescriptorSetAllocator;
struct DescriptorSetLayout;
class Device;
class Fence;
class Framebuffer;
class FramebufferAllocator;
class Image;
struct ImageCreateInfo;
class ImageView;
struct ImageViewCreateInfo;
class PipelineLayout;
class Program;
struct ProgramResourceLayout;
class RenderPass;
struct RenderPassInfo;
class Sampler;
struct SamplerCreateInfo;
class Semaphore;
class Shader;
class ShaderCompiler;
class TextureFormatLayout;
class TransientAttachmentAllocator;
class WSI;

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
template <typename T>
using HashedObject = IntrusiveHashMapEnabled<T>;

// ========================
// ===== Handle Types =====
// ========================
using BufferHandle        = IntrusivePtr<Buffer>;
using CommandBufferHandle = IntrusivePtr<CommandBuffer>;
using ContextHandle       = IntrusivePtr<Context>;
using DeviceHandle        = IntrusivePtr<Device>;
using FenceHandle         = IntrusivePtr<Fence>;
using ImageHandle         = IntrusivePtr<Image>;
using ImageViewHandle     = IntrusivePtr<ImageView>;
using SemaphoreHandle     = IntrusivePtr<Semaphore>;

// ===========================
// ===== Constant Values =====
// ===========================
constexpr int DescriptorSetsPerPool  = 16;
constexpr int MaxBindlessDescriptors = 16384;
constexpr int MaxColorAttachments    = 8;
constexpr int MaxDescriptorBindings  = 32;
constexpr int MaxDescriptorSets      = 4;
constexpr int MaxPushConstantSize    = 128;
constexpr int MaxSpecConstants       = 16;
constexpr int MaxUniformBufferSize   = 16384;
constexpr int MaxVertexAttributes    = 16;
constexpr int MaxVertexBindings      = 8;

// ===========================
// ===== Data Structures =====
// ===========================
// Contains boolean flags indicating whether or not the corresponding extension is available and enabled.
struct Extensions {
	// Instance Extensions
	bool DebugUtils              = false;
	bool GetSurfaceCapabilities2 = false;
	bool Surface                 = false;
	bool SurfaceMaintenance1     = false;
	bool SwapchainColorspace     = false;
#ifdef LUNA_VULKAN_DEBUG
	bool ValidationFeatures = false;
#endif

	// Device Extensions
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

// Contains information describing a staging buffer holding raw image data.
struct ImageInitialBuffer {
	BufferHandle Buffer;
	std::vector<vk::BufferImageCopy> Blits;
};

// Contains information describing raw image data to be uploaded to a VkImage.
struct ImageInitialData {
	const void* Data     = nullptr;
	uint32_t RowLength   = 0;
	uint32_t ImageHeight = 0;
};

struct Pipeline {
	vk::Pipeline Pipeline;
	uint32_t DynamicMask = 0;
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

struct ResourceBinding {
	union {
		vk::DescriptorBufferInfo Buffer;
		struct {
			vk::DescriptorImageInfo Float;
			vk::DescriptorImageInfo Integer;
		} Image;
		vk::BufferView BufferView;
	};
	vk::DeviceSize DynamicOffset;
	uint64_t Cookie          = 0;
	uint64_t SecondaryCookie = 0;
};

struct ResourceBindings {
	ResourceBinding Bindings[MaxDescriptorSets][MaxDescriptorBindings] = {};
	uint8_t PushConstantData[MaxPushConstantSize]                      = {0};
};

struct SwapchainConfiguration {
	vk::Extent2D Extent;
	vk::SurfaceFormatKHR Format;
	vk::PresentModeKHR PresentMode;
	vk::SurfaceTransformFlagBitsKHR Transform;
};

// =============================
// ===== Utility Functions =====
// =============================
// Calculate the number of mip levels required for a full mip chain.
uint32_t CalculateMipLevels(uint32_t width = 1, uint32_t height = 1, uint32_t depth = 1);
uint32_t CalculateMipLevels(vk::Extent2D extent);
uint32_t CalculateMipLevels(vk::Extent3D extent);
// Convert Synchronization2 flags back down to their (mostly) equivalent Synchronization1 counterparts.
vk::AccessFlags DowngradeAccessFlags2(vk::AccessFlags2 access);
vk::PipelineStageFlags DowngradeDstPipelineStageFlags2(vk::PipelineStageFlags2 stages);
vk::PipelineStageFlags DowngradePipelineStageFlags2(vk::PipelineStageFlags2 stages);
vk::PipelineStageFlags DowngradeSrcPipelineStageFlags2(vk::PipelineStageFlags2 stages);
// Returns a string formatted with KB/MB/GB size suffixes, whatever is largest.
std::string FormatSize(vk::DeviceSize size);
}  // namespace Vulkan
}  // namespace Luna
