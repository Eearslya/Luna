#pragma once

#include <Luna/Utility/IntrusivePtr.hpp>
#include <array>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include <vulkan/vulkan.hpp>

#define LUNA_VULKAN_DEBUG
#define LUNA_VULKAN_MT

namespace Luna {
namespace Vulkan {
class Context;
class WSI;
class WSIPlatform;

using ContextHandle = IntrusivePtr<Context>;

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

struct Extensions {
	// Instance Extensions
	bool DebugUtils = false;
	bool Surface    = false;
#ifdef LUNA_VULKAN_DEBUG
	bool ValidationFeatures = false;
#endif
};

struct DeviceFeatures {
	vk::PhysicalDeviceFeatures Core;
	vk::PhysicalDeviceTimelineSemaphoreFeatures TimelineSemaphore;
};

struct DeviceProperties {
	vk::PhysicalDeviceProperties Core;
	vk::PhysicalDeviceTimelineSemaphoreProperties TimelineSemaphore;
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
