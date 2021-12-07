#include <Luna/Vulkan/FormatLayout.hpp>
#include <Luna/Vulkan/Image.hpp>

namespace Luna {
namespace Vulkan {
FormatLayout::FormatLayout(vk::Format format, uint32_t width, uint32_t arrayLayers, uint32_t mipLevels)
		: _extent(width, 1, 1), _format(format), _imageType(vk::ImageType::e1D), _arrayLayers(arrayLayers), _mipLevels(1) {
	FillMipInfo();
}

FormatLayout::FormatLayout(vk::Format format, const vk::Extent2D& extent, uint32_t arrayLayers, uint32_t mipLevels)
		: _extent(extent.width, extent.height, 1),
			_format(format),
			_imageType(vk::ImageType::e2D),
			_arrayLayers(arrayLayers),
			_mipLevels(mipLevels) {
	FillMipInfo();
}

FormatLayout::FormatLayout(vk::Format format, const vk::Extent3D& extent, uint32_t mipLevels)
		: _extent(extent), _format(format), _imageType(vk::ImageType::e3D), _arrayLayers(1), _mipLevels(mipLevels) {
	FillMipInfo();
}

std::vector<vk::BufferImageCopy> FormatLayout::BuildBufferImageCopies() const {
	std::vector<vk::BufferImageCopy> copies(_mipLevels);
	for (uint32_t level = 0; level < _mipLevels; ++level) {
		const auto& mipInfo = _mips[level];

		copies[level] = vk::BufferImageCopy(mipInfo.Offset,
		                                    mipInfo.RowLength,
		                                    mipInfo.ImageHeight,
		                                    vk::ImageSubresourceLayers(FormatToAspect(_format), level, 0, _arrayLayers),
		                                    vk::Offset3D(),
		                                    mipInfo.Extent);
	}

	return copies;
}

Vec2ui FormatLayout::FormatBlockDims() const {
#define Format(fmt, w, h) \
	case vk::Format::fmt:   \
		return {              \
			w, h                \
		}
#define AstcFormat(w, h)                    \
	Format(eAstc##w##x##h##UnormBlock, w, h); \
	Format(eAstc##w##x##h##SrgbBlock, w, h);  \
	Format(eAstc##w##x##h##SfloatBlockEXT, w, h)

	switch (_format) {
		Format(eEtc2R8G8B8A8UnormBlock, 4, 4);
		Format(eEtc2R8G8B8A8SrgbBlock, 4, 4);
		Format(eEtc2R8G8B8A1UnormBlock, 4, 4);
		Format(eEtc2R8G8B8A1SrgbBlock, 4, 4);
		Format(eEtc2R8G8B8UnormBlock, 4, 4);
		Format(eEtc2R8G8B8SrgbBlock, 4, 4);
		Format(eEacR11UnormBlock, 4, 4);
		Format(eEacR11SnormBlock, 4, 4);
		Format(eEacR11G11UnormBlock, 4, 4);
		Format(eEacR11G11SnormBlock, 4, 4);

		Format(eBc1RgbUnormBlock, 4, 4);
		Format(eBc1RgbSrgbBlock, 4, 4);
		Format(eBc1RgbaUnormBlock, 4, 4);
		Format(eBc1RgbaSrgbBlock, 4, 4);
		Format(eBc2UnormBlock, 4, 4);
		Format(eBc2SrgbBlock, 4, 4);
		Format(eBc3UnormBlock, 4, 4);
		Format(eBc3SrgbBlock, 4, 4);
		Format(eBc4UnormBlock, 4, 4);
		Format(eBc4SnormBlock, 4, 4);
		Format(eBc5UnormBlock, 4, 4);
		Format(eBc5SnormBlock, 4, 4);
		Format(eBc6HUfloatBlock, 4, 4);
		Format(eBc6HSfloatBlock, 4, 4);
		Format(eBc7SrgbBlock, 4, 4);
		Format(eBc7UnormBlock, 4, 4);

		AstcFormat(4, 4);
		AstcFormat(5, 4);
		AstcFormat(5, 5);
		AstcFormat(6, 5);
		AstcFormat(6, 6);
		AstcFormat(8, 5);
		AstcFormat(8, 6);
		AstcFormat(8, 8);
		AstcFormat(10, 5);
		AstcFormat(10, 6);
		AstcFormat(10, 8);
		AstcFormat(10, 10);
		AstcFormat(12, 10);
		AstcFormat(12, 12);

		default:
			return {1, 1};
	}

#undef Format
#undef AstcFormat
}

uint32_t FormatLayout::FormatBlockSize(vk::ImageAspectFlags aspect) const {
#define Format(fmt, bpp) \
	case vk::Format::fmt:  \
		return bpp
#define Format2(fmt, bpp0, bpp1) \
	case vk::Format::fmt:          \
		return aspect == vk::ImageAspectFlagBits::ePlane0 ? bpp0 : bpp1

	switch (_format) {
		Format(eR4G4UnormPack8, 1);
		Format(eR4G4B4A4UnormPack16, 2);
		Format(eB4G4R4A4UnormPack16, 2);
		Format(eR5G6B5UnormPack16, 2);
		Format(eB5G6R5UnormPack16, 2);
		Format(eR5G5B5A1UnormPack16, 2);
		Format(eB5G5R5A1UnormPack16, 2);
		Format(eA1R5G5B5UnormPack16, 2);
		Format(eR8Unorm, 1);
		Format(eR8Snorm, 1);
		Format(eR8Uscaled, 1);
		Format(eR8Sscaled, 1);
		Format(eR8Uint, 1);
		Format(eR8Sint, 1);
		Format(eR8Srgb, 1);
		Format(eR8G8Unorm, 2);
		Format(eR8G8Snorm, 2);
		Format(eR8G8Uscaled, 2);
		Format(eR8G8Sscaled, 2);
		Format(eR8G8Uint, 2);
		Format(eR8G8Sint, 2);
		Format(eR8G8Srgb, 2);
		Format(eR8G8B8Unorm, 3);
		Format(eR8G8B8Snorm, 3);
		Format(eR8G8B8Uscaled, 3);
		Format(eR8G8B8Sscaled, 3);
		Format(eR8G8B8Uint, 3);
		Format(eR8G8B8Sint, 3);
		Format(eR8G8B8Srgb, 3);
		Format(eR8G8B8A8Unorm, 4);
		Format(eR8G8B8A8Snorm, 4);
		Format(eR8G8B8A8Uscaled, 4);
		Format(eR8G8B8A8Sscaled, 4);
		Format(eR8G8B8A8Uint, 4);
		Format(eR8G8B8A8Sint, 4);
		Format(eR8G8B8A8Srgb, 4);
		Format(eB8G8R8A8Unorm, 4);
		Format(eB8G8R8A8Snorm, 4);
		Format(eB8G8R8A8Uscaled, 4);
		Format(eB8G8R8A8Sscaled, 4);
		Format(eB8G8R8A8Uint, 4);
		Format(eB8G8R8A8Sint, 4);
		Format(eB8G8R8A8Srgb, 4);
		Format(eA8B8G8R8UnormPack32, 4);
		Format(eA8B8G8R8SnormPack32, 4);
		Format(eA8B8G8R8UscaledPack32, 4);
		Format(eA8B8G8R8SscaledPack32, 4);
		Format(eA8B8G8R8UintPack32, 4);
		Format(eA8B8G8R8SintPack32, 4);
		Format(eA8B8G8R8SrgbPack32, 4);
		Format(eA2B10G10R10UnormPack32, 4);
		Format(eA2B10G10R10SnormPack32, 4);
		Format(eA2B10G10R10UscaledPack32, 4);
		Format(eA2B10G10R10SscaledPack32, 4);
		Format(eA2B10G10R10UintPack32, 4);
		Format(eA2B10G10R10SintPack32, 4);
		Format(eA2R10G10B10UnormPack32, 4);
		Format(eA2R10G10B10SnormPack32, 4);
		Format(eA2R10G10B10UscaledPack32, 4);
		Format(eA2R10G10B10SscaledPack32, 4);
		Format(eA2R10G10B10UintPack32, 4);
		Format(eA2R10G10B10SintPack32, 4);
		Format(eR16Unorm, 2);
		Format(eR16Snorm, 2);
		Format(eR16Uscaled, 2);
		Format(eR16Sscaled, 2);
		Format(eR16Uint, 2);
		Format(eR16Sint, 2);
		Format(eR16Sfloat, 2);
		Format(eR16G16Unorm, 4);
		Format(eR16G16Snorm, 4);
		Format(eR16G16Uscaled, 4);
		Format(eR16G16Sscaled, 4);
		Format(eR16G16Uint, 4);
		Format(eR16G16Sint, 4);
		Format(eR16G16Sfloat, 4);
		Format(eR16G16B16Unorm, 6);
		Format(eR16G16B16Snorm, 6);
		Format(eR16G16B16Uscaled, 6);
		Format(eR16G16B16Sscaled, 6);
		Format(eR16G16B16Uint, 6);
		Format(eR16G16B16Sint, 6);
		Format(eR16G16B16Sfloat, 6);
		Format(eR16G16B16A16Unorm, 8);
		Format(eR16G16B16A16Snorm, 8);
		Format(eR16G16B16A16Uscaled, 8);
		Format(eR16G16B16A16Sscaled, 8);
		Format(eR16G16B16A16Uint, 8);
		Format(eR16G16B16A16Sint, 8);
		Format(eR16G16B16A16Sfloat, 8);
		Format(eR32Uint, 4);
		Format(eR32Sint, 4);
		Format(eR32Sfloat, 4);
		Format(eR32G32Uint, 8);
		Format(eR32G32Sint, 8);
		Format(eR32G32Sfloat, 8);
		Format(eR32G32B32Uint, 12);
		Format(eR32G32B32Sint, 12);
		Format(eR32G32B32Sfloat, 12);
		Format(eR32G32B32A32Uint, 16);
		Format(eR32G32B32A32Sint, 16);
		Format(eR32G32B32A32Sfloat, 16);
		Format(eR64Uint, 8);
		Format(eR64Sint, 8);
		Format(eR64Sfloat, 8);
		Format(eR64G64Uint, 16);
		Format(eR64G64Sint, 16);
		Format(eR64G64Sfloat, 16);
		Format(eR64G64B64Uint, 24);
		Format(eR64G64B64Sint, 24);
		Format(eR64G64B64Sfloat, 24);
		Format(eR64G64B64A64Uint, 32);
		Format(eR64G64B64A64Sint, 32);
		Format(eR64G64B64A64Sfloat, 32);
		Format(eB10G11R11UfloatPack32, 4);
		Format(eE5B9G9R9UfloatPack32, 4);

		Format(eD16Unorm, 2);
		Format(eX8D24UnormPack32, 4);
		Format(eD32Sfloat, 4);
		Format(eS8Uint, 1);

		case vk::Format::eD16UnormS8Uint:
			return aspect == vk::ImageAspectFlagBits::eDepth ? 2 : 1;
		case vk::Format::eD24UnormS8Uint:
		case vk::Format::eD32SfloatS8Uint:
			return aspect == vk::ImageAspectFlagBits::eDepth ? 4 : 1;

			// ETC2
			Format(eEtc2R8G8B8A8UnormBlock, 16);
			Format(eEtc2R8G8B8A8SrgbBlock, 16);
			Format(eEtc2R8G8B8A1UnormBlock, 8);
			Format(eEtc2R8G8B8A1SrgbBlock, 8);
			Format(eEtc2R8G8B8UnormBlock, 8);
			Format(eEtc2R8G8B8SrgbBlock, 8);
			Format(eEacR11UnormBlock, 8);
			Format(eEacR11SnormBlock, 8);
			Format(eEacR11G11UnormBlock, 16);
			Format(eEacR11G11SnormBlock, 16);

			// BC
			Format(eBc1RgbUnormBlock, 8);
			Format(eBc1RgbSrgbBlock, 8);
			Format(eBc1RgbaUnormBlock, 8);
			Format(eBc1RgbaSrgbBlock, 8);
			Format(eBc2UnormBlock, 16);
			Format(eBc2SrgbBlock, 16);
			Format(eBc3UnormBlock, 16);
			Format(eBc3SrgbBlock, 16);
			Format(eBc4UnormBlock, 8);
			Format(eBc4SnormBlock, 8);
			Format(eBc5UnormBlock, 16);
			Format(eBc5SnormBlock, 16);
			Format(eBc6HUfloatBlock, 16);
			Format(eBc6HSfloatBlock, 16);
			Format(eBc7SrgbBlock, 16);
			Format(eBc7UnormBlock, 16);

			// ASTC
#define AstcFormat(w, h)                  \
	Format(eAstc##w##x##h##UnormBlock, 16); \
	Format(eAstc##w##x##h##SrgbBlock, 16);  \
	Format(eAstc##w##x##h##SfloatBlockEXT, 16)

			AstcFormat(4, 4);
			AstcFormat(5, 4);
			AstcFormat(5, 5);
			AstcFormat(6, 5);
			AstcFormat(6, 6);
			AstcFormat(8, 5);
			AstcFormat(8, 6);
			AstcFormat(8, 8);
			AstcFormat(10, 5);
			AstcFormat(10, 6);
			AstcFormat(10, 8);
			AstcFormat(10, 10);
			AstcFormat(12, 10);
			AstcFormat(12, 12);

			Format(eG8B8G8R8422Unorm, 4);
			Format(eB8G8R8G8422Unorm, 4);

			Format(eG8B8R83Plane420Unorm, 1);
			Format2(eG8B8R82Plane420Unorm, 1, 2);
			Format(eG8B8R83Plane422Unorm, 1);
			Format2(eG8B8R82Plane422Unorm, 1, 2);
			Format(eG8B8R83Plane444Unorm, 1);

			Format(eR10X6UnormPack16, 2);
			Format(eR10X6G10X6Unorm2Pack16, 4);
			Format(eR10X6G10X6B10X6A10X6Unorm4Pack16, 8);
			Format(eG10X6B10X6G10X6R10X6422Unorm4Pack16, 8);
			Format(eB10X6G10X6R10X6G10X6422Unorm4Pack16, 8);
			Format(eG10X6B10X6R10X63Plane420Unorm3Pack16, 2);
			Format(eG10X6B10X6R10X63Plane422Unorm3Pack16, 2);
			Format(eG10X6B10X6R10X63Plane444Unorm3Pack16, 2);
			Format2(eG10X6B10X6R10X62Plane420Unorm3Pack16, 2, 4);
			Format2(eG10X6B10X6R10X62Plane422Unorm3Pack16, 2, 4);

			Format(eR12X4UnormPack16, 2);
			Format(eR12X4G12X4Unorm2Pack16, 4);
			Format(eR12X4G12X4B12X4A12X4Unorm4Pack16, 8);
			Format(eG12X4B12X4G12X4R12X4422Unorm4Pack16, 8);
			Format(eB12X4G12X4R12X4G12X4422Unorm4Pack16, 8);
			Format(eG12X4B12X4R12X43Plane420Unorm3Pack16, 2);
			Format(eG12X4B12X4R12X43Plane422Unorm3Pack16, 2);
			Format(eG12X4B12X4R12X43Plane444Unorm3Pack16, 2);
			Format2(eG12X4B12X4R12X42Plane420Unorm3Pack16, 2, 4);
			Format2(eG12X4B12X4R12X42Plane422Unorm3Pack16, 2, 4);

			Format(eG16B16G16R16422Unorm, 8);
			Format(eB16G16R16G16422Unorm, 8);
			Format(eG16B16R163Plane420Unorm, 2);
			Format(eG16B16R163Plane422Unorm, 2);
			Format(eG16B16R163Plane444Unorm, 2);
			Format2(eG16B16R162Plane420Unorm, 2, 4);
			Format2(eG16B16R162Plane422Unorm, 2, 4);

		default:
			assert(0 && "Unknown format.");
			return 0;
	}
#undef Format
#undef Format2
#undef AstcFormat
}

size_t FormatLayout::LayerByteStride(uint32_t imageHeight, size_t rowByteStride) const {
	return ((imageHeight + _blockDims.y - 1) / _blockDims.y) * rowByteStride;
}

size_t FormatLayout::RowByteStride(uint32_t rowLength) const {
	return ((rowLength + _blockDims.x - 1) / _blockDims.x) * _blockStride;
}

vk::Extent3D FormatLayout::GetExtent(uint32_t mip) const {
	return _mips[mip].Extent;
}

size_t FormatLayout::GetLayerSize(uint32_t mip) const {
	return _mips[mip].BlockImageHeight * GetRowSize(mip);
}

size_t FormatLayout::GetRowSize(uint32_t mip) const {
	return _mips[mip].BlockRowLength * _blockStride;
}

void FormatLayout::SetBuffer(void* buffer, size_t size) {
	_buffer     = static_cast<uint8_t*>(buffer);
	_bufferSize = size;
}

void FormatLayout::FillMipInfo() {
	_blockStride = FormatBlockSize({});
	_blockDims   = FormatBlockDims();

	if (_mipLevels == 0) { _mipLevels = CalculateMipLevels(_extent); }

	uint32_t width  = _extent.width;
	uint32_t height = _extent.height;
	uint32_t depth  = _extent.depth;

	size_t offset = 0;
	for (uint32_t mip = 0; mip < _mipLevels; ++mip) {
		offset = (offset + 15) & ~15;

		const uint32_t blocksX = (width + _blockDims.x - 1) / _blockDims.x;
		const uint32_t blocksY = (height + _blockDims.y - 1) / _blockDims.y;
		const size_t mipSize   = blocksX * blocksY * _arrayLayers * depth * _blockStride;

		_mips[mip] = {.Offset           = offset,
		              .Extent           = {width, height, depth},
		              .BlockImageHeight = blocksY,
		              .BlockRowLength   = blocksX,
		              .ImageHeight      = blocksY * _blockDims.y,
		              .RowLength        = blocksX * _blockDims.x};
		offset += mipSize;

		width  = std::max(width >> 1u, 1u);
		height = std::max(height >> 1u, 1u);
		depth  = std::max(depth >> 1u, 1u);
	}

	_requiredSize = offset;
}
}  // namespace Vulkan
}  // namespace Luna
