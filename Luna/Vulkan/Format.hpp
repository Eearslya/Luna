#pragma once

#include "Common.hpp"
#include "TextureFormat.hpp"

namespace Luna {
namespace Vulkan {
inline FormatCompressionType GetFormatCompressionType(vk::Format format) {
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

#define AstcFmt(w, h)                          \
	case vk::Format::eAstc##w##x##h##UnormBlock: \
	case vk::Format::eAstc##w##x##h##SrgbBlock:  \
	case vk::Format::eAstc##w##x##h##SfloatBlockEXT:
			AstcFmt(4, 4) AstcFmt(5, 4) AstcFmt(5, 5) AstcFmt(6, 5) AstcFmt(6, 6) AstcFmt(8, 5) AstcFmt(8, 6) AstcFmt(8, 8)
				AstcFmt(10, 5) AstcFmt(10, 6) AstcFmt(10, 8) AstcFmt(10, 10) AstcFmt(12, 10)
					AstcFmt(12, 12) return FormatCompressionType::ASTC;
#undef AstcFmt

		default:
			return FormatCompressionType::Uncompressed;
	}
}

inline bool FormatIsCompressedHDR(vk::Format format) {
	switch (format) {
#define AstcFmt(w, h) case vk::Format::eAstc##w##x##h##SfloatBlockEXT
		AstcFmt(4, 4)
				: AstcFmt(5, 4)
				: AstcFmt(5, 5)
				: AstcFmt(6, 5)
				: AstcFmt(6, 6)
				: AstcFmt(8, 5)
				: AstcFmt(8, 6)
				: AstcFmt(8, 8)
				: AstcFmt(10, 5)
				: AstcFmt(10, 6)
				: AstcFmt(10, 8)
				: AstcFmt(10, 10)
				: AstcFmt(12, 10)
				: AstcFmt(12, 12)
				:
#undef AstcFmt
					return true;

		case vk::Format::eBc6HSfloatBlock:
		case vk::Format::eBc6HUfloatBlock:
			return true;

		default:
			return false;
	}
}

inline bool FormatIsSrgb(vk::Format format) {
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

inline bool FormatHasDepth(vk::Format format) {
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

inline bool FormatHasStencil(vk::Format format) {
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

inline bool FormatHasDepthOrStencil(vk::Format format) {
	return FormatHasDepth(format) || FormatHasStencil(format);
}

inline vk::ImageAspectFlags FormatToAspect(vk::Format format) {
	switch (format) {
		case vk::Format::eUndefined:
			return {};

		case vk::Format::eS8Uint:
			return vk::ImageAspectFlagBits::eStencil;

		case vk::Format::eD16UnormS8Uint:
		case vk::Format::eD24UnormS8Uint:
		case vk::Format::eD32SfloatS8Uint:
			return vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil;

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
