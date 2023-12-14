#pragma once

#include <Luna/Vulkan/Common.hpp>

namespace Luna {
namespace Vulkan {
void FormatAlignDimensions(vk::Format format, uint32_t& width, uint32_t& height);
vk::ImageAspectFlags FormatAspectFlags(vk::Format format);
void FormatBlockCount(vk::Format& format, uint32_t& width, uint32_t& height);
int FormatChannelCount(vk::Format format);
bool FormatIsSrgb(vk::Format format);

constexpr bool FormatHasDepth(vk::Format format) {
	switch (format) {
		case vk::Format::eD16Unorm:
		case vk::Format::eD16UnormS8Uint:
		case vk::Format::eD24UnormS8Uint:
		case vk::Format::eD32Sfloat:
		case vk::Format::eX8D24UnormPack32:
		case vk::Format::eD32SfloatS8Uint:
			return true;

		default:
			return false;
	}
}

constexpr bool FormatHasStencil(vk::Format format) {
	switch (format) {
		case vk::Format::eS8Uint:
		case vk::Format::eD16UnormS8Uint:
		case vk::Format::eD24UnormS8Uint:
		case vk::Format::eD32SfloatS8Uint:
			return true;

		default:
			return false;
	}
}

constexpr bool FormatHasDepthOrStencil(vk::Format format) {
	return FormatHasDepth(format) || FormatHasStencil(format);
}
}  // namespace Vulkan
}  // namespace Luna
