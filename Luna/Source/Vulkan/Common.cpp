#include <Luna/Vulkan/Common.hpp>
#include <algorithm>
#include <sstream>

namespace Luna {
namespace Vulkan {
uint32_t CalculateMipLevels(uint32_t width, uint32_t height, uint32_t depth) {
	return std::floor(std::log2(std::max(std::max(width, height), depth))) + 1;
}

uint32_t CalculateMipLevels(vk::Extent2D extent) {
	return CalculateMipLevels(extent.width, extent.height, 1);
}

uint32_t CalculateMipLevels(vk::Extent3D extent) {
	return CalculateMipLevels(extent.width, extent.height, extent.depth);
}

std::string FormatSize(vk::DeviceSize size) {
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
