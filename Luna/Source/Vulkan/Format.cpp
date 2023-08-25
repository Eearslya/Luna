#include <Luna/Vulkan/Format.hpp>
#include <vulkan/vulkan_format_traits.hpp>

namespace Luna {
namespace Vulkan {
void FormatAlignDimensions(vk::Format format, uint32_t& width, uint32_t& height) {}

vk::ImageAspectFlags FormatAspectFlags(vk::Format format) {
	switch (format) {
		case vk::Format::eUndefined:
			return {};

		case vk::Format::eS8Uint:
			return vk::ImageAspectFlagBits::eStencil;

		case vk::Format::eD16Unorm:
		case vk::Format::eD32Sfloat:
		case vk::Format::eX8D24UnormPack32:
			return vk::ImageAspectFlagBits::eDepth;

		case vk::Format::eD16UnormS8Uint:
		case vk::Format::eD24UnormS8Uint:
		case vk::Format::eD32SfloatS8Uint:
			return vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil;

		default:
			return vk::ImageAspectFlagBits::eColor;
	}
}

void FormatBlockCount(vk::Format& format, uint32_t& width, uint32_t& height) {}

int FormatChannelCount(vk::Format format) {
	return vk::componentCount(format);
}

bool FormatHasDepth(vk::Format format) {
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

bool FormatHasStencil(vk::Format format) {
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

bool FormatHasDepthOrStencil(vk::Format format) {
	return FormatHasDepth(format) || FormatHasStencil(format);
}

bool FormatIsSrgb(vk::Format format) {
	const char* comp = vk::componentName(format, 0);

	return strcmp(comp, "SRGB") == 0;
}
}  // namespace Vulkan
}  // namespace Luna
