#include <Luna/Vulkan/Format.hpp>
#include <Luna/Vulkan/TextureFormat.hpp>
#include <vulkan/vulkan_format_traits.hpp>

namespace Luna {
namespace Vulkan {
void FormatAlignDim(vk::Format format, uint32_t& width, uint32_t& height) {
	uint32_t w, h;
	TextureFormatLayout::FormatBlockDim(format, w, h);
	width  = ((width + w - 1) / w) * w;
	height = ((height + h - 1) / h) * h;
}

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

void FormatBlockCount(vk::Format format, uint32_t& width, uint32_t& height) {
	uint32_t w, h;
	TextureFormatLayout::FormatBlockDim(format, w, h);
	width  = (width + w - 1) / w;
	height = (height + h - 1) / h;
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

FormatCompressionType GetFormatCompressionType(vk::Format format) {
	const char* compression = vk::compressionScheme(format);

	if (strcmp(compression, "BC") == 0) {
		return FormatCompressionType::BC;
	} else if (strcmp(compression, "ETC2") == 0 || strcmp(compression, "EAC") == 0) {
		return FormatCompressionType::ETC;
	} else if (strcmp(compression, "ASTC LDR") == 0 || strcmp(compression, "ASTC HDR") == 0) {
		return FormatCompressionType::ASTC;
	} else {
		return FormatCompressionType::Uncompressed;
	}
}

vk::DeviceSize GetFormatLayerSize(
	vk::Format format, vk::ImageAspectFlags aspect, uint32_t width, uint32_t height, uint32_t depth) {
	uint32_t blocksX = width;
	uint32_t blocksY = height;
	FormatBlockCount(format, blocksX, blocksY);

	return TextureFormatLayout::FormatBlockSize(format, aspect) * depth * blocksX * blocksY;
}

bool IsFormatCompressedHDR(vk::Format format) {
	const char* compression = vk::compressionScheme(format);

	return strcmp(compression, "ASTC HDR") == 0;
}

bool IsFormatSrgb(vk::Format format) {
	const char* comp = vk::componentName(format, 0);

	return strcmp(comp, "SRGB") == 0;
}
}  // namespace Vulkan
}  // namespace Luna
