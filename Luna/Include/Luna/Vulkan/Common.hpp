#pragma once

#include <Luna/Utility/NonCopyable.hpp>
#include <array>
#include <set>
#include <unordered_map>
#include <vector>
#include <vulkan/vulkan.hpp>

namespace Luna {
namespace Vulkan {
// Forward declarations.
class Context;

// Enums and constants.
enum class QueueType { Graphics, Transfer, Compute };
constexpr static const int QueueTypeCount = 3;

// Structures
struct ExtensionInfo {
	bool DebugUtils                   = false;
	bool GetPhysicalDeviceProperties2 = false;
	bool GetSurfaceCapabilities2      = false;
	bool TimelineSemaphore            = false;
	bool ValidationFeatures           = false;
};
struct GPUFeatures {
	vk::PhysicalDeviceFeatures Features;
	vk::PhysicalDeviceTimelineSemaphoreFeatures TimelineSemaphore;
};
struct GPUProperties {
	vk::PhysicalDeviceProperties Properties;
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
inline const char* QueueTypeName(QueueType type) {
	switch (type) {
		case QueueType::Graphics:
			return "Graphics";
		case QueueType::Transfer:
			return "Transfer";
		case QueueType::Compute:
			return "Compute";
	}

	return "Unknown";
}
}  // namespace Vulkan
}  // namespace Luna
