#pragma once

#include <Luna/Utility/NonCopyable.hpp>
#include <set>
#include <unordered_map>
#include <vector>
#include <vulkan/vulkan.hpp>

namespace Luna {
namespace Vulkan {
// Forward declarations.
class Context;

// Structures
struct ExtensionInfo {
	bool DebugUtils                   = false;
	bool GetPhysicalDeviceProperties2 = false;
	bool GetSurfaceCapabilities2      = false;
	bool TimelineSemaphore            = false;
	bool ValidationFeatures           = false;
};
}  // namespace Vulkan
}  // namespace Luna
