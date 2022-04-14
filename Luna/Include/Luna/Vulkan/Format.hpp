#pragma once

#include <Luna/Vulkan/Common.hpp>

namespace Luna {
namespace Vulkan {
static inline FormatCompressionType GetFormatCompressionType(vk::Format format) {
	switch (format) {
		case vk::Format::eBc1RgbaSrgbBlock:
		case vk::Format::eBc1RgbaUnormBlock:
		case vk::Format::eBc1RgbSrgbBlock:
		case vk::Format::eBc1RgbUnormBlock:
		case vk::Format::eBc2SrgbBlock:
		case vk::Format::eBc2UnormBlock:
		case vk::Format::eBc3SrgbBlock:
		case vk::Format::eBc3UnormBlock:
		case vk::Format::eBc4UnormBlock:
		case vk::Format::eBc4SnormBlock:
		case vk::Format::eBc5UnormBlock:
		case vk::Format::eBc5SnormBlock:
		case vk::Format::eBc6HSfloatBlock:
		case vk::Format::eBc6HUfloatBlock:
		case vk::Format::eBc7SrgbBlock:
		case vk::Format::eBc7UnormBlock:
			return FormatCompressionType::BC;

		case vk::Format::eEtc2R8G8B8A1SrgbBlock:
		case vk::Format::eEtc2R8G8B8A1UnormBlock:
		case vk::Format::eEtc2R8G8B8A8SrgbBlock:
		case vk::Format::eEtc2R8G8B8A8UnormBlock:
		case vk::Format::eEtc2R8G8B8SrgbBlock:
		case vk::Format::eEtc2R8G8B8UnormBlock:
		case vk::Format::eEacR11G11SnormBlock:
		case vk::Format::eEacR11G11UnormBlock:
		case vk::Format::eEacR11SnormBlock:
		case vk::Format::eEacR11UnormBlock:
			return FormatCompressionType::ETC;

#define ASTCFormat(w, h)                       \
	case vk::Format::eAstc##w##x##h##UnormBlock: \
	case vk::Format::eAstc##w##x##h##SrgbBlock:  \
	case vk::Format::eAstc##w##x##h##SfloatBlockEXT:
			ASTCFormat(4, 4) ASTCFormat(5, 4) ASTCFormat(5, 5) ASTCFormat(6, 5) ASTCFormat(6, 6) ASTCFormat(8, 5)
				ASTCFormat(8, 6) ASTCFormat(8, 8) ASTCFormat(10, 5) ASTCFormat(10, 6) ASTCFormat(10, 8) ASTCFormat(10, 10)
					ASTCFormat(12, 10) ASTCFormat(12, 12) return FormatCompressionType::ASTC;
#undef ASTCFormat

		default:
			return FormatCompressionType::Uncompressed;
	}
}

static inline bool FormatIsCompressedHDR(vk::Format format) {
	switch (format) {
#define ASTCFormat(w, h) case vk::Format::eAstc##w##x##h##SfloatBlockEXT:
		ASTCFormat(4, 4) ASTCFormat(5, 4) ASTCFormat(5, 5) ASTCFormat(6, 5) ASTCFormat(6, 6) ASTCFormat(8, 5)
			ASTCFormat(8, 6) ASTCFormat(8, 8) ASTCFormat(10, 5) ASTCFormat(10, 6) ASTCFormat(10, 8) ASTCFormat(10, 10)
				ASTCFormat(12, 10) ASTCFormat(12, 12)
#undef ASTCFormat
					return true;

		case vk::Format::eBc6HSfloatBlock:
		case vk::Format::eBc6HUfloatBlock:
			return true;

		default:
			return false;
	}
}

static inline bool FormatIsSrgb(vk::Format format) {
	switch (format) {
		case vk::Format::eA8B8G8R8SrgbPack32:
		case vk::Format::eR8G8B8A8Srgb:
		case vk::Format::eB8G8R8A8Srgb:
		case vk::Format::eR8Srgb:
		case vk::Format::eR8G8Srgb:
		case vk::Format::eR8G8B8Srgb:
		case vk::Format::eB8G8R8Srgb:
		case vk::Format::eBc1RgbSrgbBlock:
		case vk::Format::eBc1RgbaSrgbBlock:
		case vk::Format::eBc2SrgbBlock:
		case vk::Format::eBc3SrgbBlock:
		case vk::Format::eBc7SrgbBlock:
		case vk::Format::eEtc2R8G8B8SrgbBlock:
		case vk::Format::eEtc2R8G8B8A1SrgbBlock:
		case vk::Format::eEtc2R8G8B8A8SrgbBlock:
		case vk::Format::eAstc4x4SrgbBlock:
		case vk::Format::eAstc5x4SrgbBlock:
		case vk::Format::eAstc5x5SrgbBlock:
		case vk::Format::eAstc6x5SrgbBlock:
		case vk::Format::eAstc6x6SrgbBlock:
		case vk::Format::eAstc8x5SrgbBlock:
		case vk::Format::eAstc8x6SrgbBlock:
		case vk::Format::eAstc8x8SrgbBlock:
		case vk::Format::eAstc10x5SrgbBlock:
		case vk::Format::eAstc10x6SrgbBlock:
		case vk::Format::eAstc10x8SrgbBlock:
		case vk::Format::eAstc10x10SrgbBlock:
		case vk::Format::eAstc12x10SrgbBlock:
		case vk::Format::eAstc12x12SrgbBlock:
			return true;

		default:
			return false;
	}
}

static inline bool FormatHasDepth(vk::Format format) {
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

static inline bool FormatHasStencil(vk::Format format) {
	switch (format) {
		case vk::Format::eD16UnormS8Uint:
		case vk::Format::eD24UnormS8Uint:
		case vk::Format::eD32SfloatS8Uint:
		case vk::Format::eS8Uint:
			return true;

		default:
			return false;
	}
}

static inline bool FormatHasDepthOrStencil(vk::Format format) {
	return FormatHasDepth(format) || FormatHasStencil(format);
}

static inline vk::ImageAspectFlags FormatToAspect(vk::Format format) {
	switch (format) {
		case vk::Format::eUndefined:
			return {};

		case vk::Format::eS8Uint:
			return vk::ImageAspectFlagBits::eStencil;

		case vk::Format::eD16UnormS8Uint:
		case vk::Format::eD24UnormS8Uint:
		case vk::Format::eD32SfloatS8Uint:
			return vk::ImageAspectFlagBits::eStencil | vk::ImageAspectFlagBits::eDepth;

		case vk::Format::eD16Unorm:
		case vk::Format::eD32Sfloat:
		case vk::Format::eX8D24UnormPack32:
			return vk::ImageAspectFlagBits::eDepth;

		default:
			return vk::ImageAspectFlagBits::eColor;
	}
}
}  // namespace Vulkan
}  // namespace Luna
