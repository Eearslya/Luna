#include <Luna/Vulkan/Format.hpp>
#include <Luna/Vulkan/TextureFormat.hpp>

namespace Luna {
namespace Vulkan {
std::vector<vk::BufferImageCopy> TextureFormatLayout::BuildBufferImageCopies() const {
	std::vector<vk::BufferImageCopy> copies(_mipLevels);
	for (uint32_t level = 0; level < _mipLevels; ++level) {
		const auto& info = _mipInfos[level];

		copies[level] = vk::BufferImageCopy(info.Offset,
		                                    info.RowLength,
		                                    info.ImageHeight,
		                                    vk::ImageSubresourceLayers(FormatAspectFlags(_format), level, 0, _arrayLayers),
		                                    vk::Offset3D(0, 0, 0),
		                                    vk::Extent3D(info.Width, info.Height, info.Depth));
	}

	return copies;
}

void TextureFormatLayout::Set1D(vk::Format format, uint32_t width, uint32_t arrayLayers, uint32_t mipLevels) {
	_imageType   = vk::ImageType::e1D;
	_format      = format;
	_arrayLayers = arrayLayers;
	_mipLevels   = mipLevels;

	FillMipInfo(width, 1, 1);
}

void TextureFormatLayout::Set2D(
	vk::Format format, uint32_t width, uint32_t height, uint32_t arrayLayers, uint32_t mipLevels) {
	_imageType   = vk::ImageType::e2D;
	_format      = format;
	_arrayLayers = arrayLayers;
	_mipLevels   = mipLevels;

	FillMipInfo(width, height, 1);
}

void TextureFormatLayout::Set3D(
	vk::Format format, uint32_t width, uint32_t height, uint32_t depth, uint32_t mipLevels) {
	_imageType   = vk::ImageType::e3D;
	_format      = format;
	_arrayLayers = 1;
	_mipLevels   = mipLevels;

	FillMipInfo(width, height, depth);
}

void TextureFormatLayout::SetBuffer(size_t size, void* buffer) {
	_bufferSize = size;
	_buffer     = reinterpret_cast<uint8_t*>(buffer);
}

void TextureFormatLayout::FillMipInfo(uint32_t width, uint32_t height, uint32_t depth) {
	_blockStride = FormatBlockSize(_format, {});
	FormatBlockDim(_format, _blockDimX, _blockDimY);

	if (_mipLevels == 0) { _mipLevels = CalculateMipLevels(width, height, depth); }

	size_t offset = 0;
	for (uint32_t mip = 0; mip < _mipLevels; ++mip) {
		offset = (offset + 15) & ~15;

		const uint32_t blocksX = (width + _blockDimX - 1) / _blockDimX;
		const uint32_t blocksY = (height + _blockDimY - 1) / _blockDimY;
		const size_t mipSize   = blocksX * blocksY * _arrayLayers * depth * _blockStride;

		_mipInfos[mip].Offset           = offset;
		_mipInfos[mip].BlockRowLength   = blocksX;
		_mipInfos[mip].BlockImageHeight = blocksY;
		_mipInfos[mip].RowLength        = blocksX * _blockDimX;
		_mipInfos[mip].ImageHeight      = blocksY * _blockDimY;
		_mipInfos[mip].Width            = width;
		_mipInfos[mip].Height           = height;
		_mipInfos[mip].Depth            = depth;

		offset += mipSize;
		width  = std::max(width >> 1u, 1u);
		height = std::max(height >> 1u, 1u);
		depth  = std::max(depth >> 1u, 1u);
	}

	_requiredSize = offset;
}

void TextureFormatLayout::FormatBlockDim(vk::Format format, uint32_t& width, uint32_t& height) {
#define Fmt(x, w, h)     \
	case vk::Format::e##x: \
		width  = w;          \
		height = h;          \
		break

#define AstcFmt(w, h)                   \
	Fmt(Astc##w##x##h##UnormBlock, w, h); \
	Fmt(Astc##w##x##h##SrgbBlock, w, h);  \
	Fmt(Astc##w##x##h##SfloatBlockEXT, w, h);

	switch (format) {
		Fmt(Etc2R8G8B8A8UnormBlock, 4, 4);
		Fmt(Etc2R8G8B8A8SrgbBlock, 4, 4);
		Fmt(Etc2R8G8B8A1UnormBlock, 4, 4);
		Fmt(Etc2R8G8B8A1SrgbBlock, 4, 4);
		Fmt(Etc2R8G8B8UnormBlock, 4, 4);
		Fmt(Etc2R8G8B8SrgbBlock, 4, 4);
		Fmt(EacR11UnormBlock, 4, 4);
		Fmt(EacR11SnormBlock, 4, 4);
		Fmt(EacR11G11UnormBlock, 4, 4);
		Fmt(EacR11G11SnormBlock, 4, 4);
		Fmt(Bc1RgbUnormBlock, 4, 4);
		Fmt(Bc1RgbSrgbBlock, 4, 4);
		Fmt(Bc1RgbaUnormBlock, 4, 4);
		Fmt(Bc1RgbaSrgbBlock, 4, 4);
		Fmt(Bc2UnormBlock, 4, 4);
		Fmt(Bc2SrgbBlock, 4, 4);
		Fmt(Bc3UnormBlock, 4, 4);
		Fmt(Bc3SrgbBlock, 4, 4);
		Fmt(Bc4UnormBlock, 4, 4);
		Fmt(Bc4SnormBlock, 4, 4);
		Fmt(Bc5UnormBlock, 4, 4);
		Fmt(Bc5SnormBlock, 4, 4);
		Fmt(Bc6HUfloatBlock, 4, 4);
		Fmt(Bc6HSfloatBlock, 4, 4);
		Fmt(Bc7SrgbBlock, 4, 4);
		Fmt(Bc7UnormBlock, 4, 4);

		AstcFmt(4, 4);
		AstcFmt(5, 4);
		AstcFmt(5, 5);
		AstcFmt(6, 5);
		AstcFmt(6, 6);
		AstcFmt(8, 5);
		AstcFmt(8, 6);
		AstcFmt(8, 8);
		AstcFmt(10, 5);
		AstcFmt(10, 6);
		AstcFmt(10, 8);
		AstcFmt(10, 10);
		AstcFmt(12, 10);
		AstcFmt(12, 12);

		default:
			width  = 1;
			height = 1;
			break;
	}

#undef AstcFmt
#undef Fmt
}

uint32_t TextureFormatLayout::FormatBlockSize(vk::Format format, vk::ImageAspectFlags aspect) {
#define Fmt(x, bpp)      \
	case vk::Format::e##x: \
		return bpp
#define Fmt2(x, bpp0, bpp1) \
	case vk::Format::e##x:    \
		return aspect == vk::ImageAspectFlagBits::ePlane0 ? bpp0 : bpp1
#define AstcFmt(w, h)                 \
	Fmt(Astc##w##x##h##UnormBlock, 16); \
	Fmt(Astc##w##x##h##SrgbBlock, 16);  \
	Fmt(Astc##w##x##h##SfloatBlockEXT, 16);

	switch (format) {
		Fmt(R4G4UnormPack8, 1);
		Fmt(R4G4B4A4UnormPack16, 2);
		Fmt(B4G4R4A4UnormPack16, 2);
		Fmt(R5G6B5UnormPack16, 2);
		Fmt(B5G6R5UnormPack16, 2);
		Fmt(R5G5B5A1UnormPack16, 2);
		Fmt(B5G5R5A1UnormPack16, 2);
		Fmt(A1R5G5B5UnormPack16, 2);
		Fmt(R8Unorm, 1);
		Fmt(R8Snorm, 1);
		Fmt(R8Uscaled, 1);
		Fmt(R8Sscaled, 1);
		Fmt(R8Uint, 1);
		Fmt(R8Sint, 1);
		Fmt(R8Srgb, 1);
		Fmt(R8G8Unorm, 2);
		Fmt(R8G8Snorm, 2);
		Fmt(R8G8Uscaled, 2);
		Fmt(R8G8Sscaled, 2);
		Fmt(R8G8Uint, 2);
		Fmt(R8G8Sint, 2);
		Fmt(R8G8Srgb, 2);
		Fmt(R8G8B8Unorm, 3);
		Fmt(R8G8B8Snorm, 3);
		Fmt(R8G8B8Uscaled, 3);
		Fmt(R8G8B8Sscaled, 3);
		Fmt(R8G8B8Uint, 3);
		Fmt(R8G8B8Sint, 3);
		Fmt(R8G8B8Srgb, 3);
		Fmt(R8G8B8A8Unorm, 4);
		Fmt(R8G8B8A8Snorm, 4);
		Fmt(R8G8B8A8Uscaled, 4);
		Fmt(R8G8B8A8Sscaled, 4);
		Fmt(R8G8B8A8Uint, 4);
		Fmt(R8G8B8A8Sint, 4);
		Fmt(R8G8B8A8Srgb, 4);
		Fmt(B8G8R8A8Unorm, 4);
		Fmt(B8G8R8A8Snorm, 4);
		Fmt(B8G8R8A8Uscaled, 4);
		Fmt(B8G8R8A8Sscaled, 4);
		Fmt(B8G8R8A8Uint, 4);
		Fmt(B8G8R8A8Sint, 4);
		Fmt(B8G8R8A8Srgb, 4);
		Fmt(A8B8G8R8UnormPack32, 4);
		Fmt(A8B8G8R8SnormPack32, 4);
		Fmt(A8B8G8R8UscaledPack32, 4);
		Fmt(A8B8G8R8SscaledPack32, 4);
		Fmt(A8B8G8R8UintPack32, 4);
		Fmt(A8B8G8R8SintPack32, 4);
		Fmt(A8B8G8R8SrgbPack32, 4);
		Fmt(A2B10G10R10UnormPack32, 4);
		Fmt(A2B10G10R10SnormPack32, 4);
		Fmt(A2B10G10R10UscaledPack32, 4);
		Fmt(A2B10G10R10SscaledPack32, 4);
		Fmt(A2B10G10R10UintPack32, 4);
		Fmt(A2B10G10R10SintPack32, 4);
		Fmt(A2R10G10B10UnormPack32, 4);
		Fmt(A2R10G10B10SnormPack32, 4);
		Fmt(A2R10G10B10UscaledPack32, 4);
		Fmt(A2R10G10B10SscaledPack32, 4);
		Fmt(A2R10G10B10UintPack32, 4);
		Fmt(A2R10G10B10SintPack32, 4);
		Fmt(R16Unorm, 2);
		Fmt(R16Snorm, 2);
		Fmt(R16Uscaled, 2);
		Fmt(R16Sscaled, 2);
		Fmt(R16Uint, 2);
		Fmt(R16Sint, 2);
		Fmt(R16Sfloat, 2);
		Fmt(R16G16Unorm, 4);
		Fmt(R16G16Snorm, 4);
		Fmt(R16G16Uscaled, 4);
		Fmt(R16G16Sscaled, 4);
		Fmt(R16G16Uint, 4);
		Fmt(R16G16Sint, 4);
		Fmt(R16G16Sfloat, 4);
		Fmt(R16G16B16Unorm, 6);
		Fmt(R16G16B16Snorm, 6);
		Fmt(R16G16B16Uscaled, 6);
		Fmt(R16G16B16Sscaled, 6);
		Fmt(R16G16B16Uint, 6);
		Fmt(R16G16B16Sint, 6);
		Fmt(R16G16B16Sfloat, 6);
		Fmt(R16G16B16A16Unorm, 8);
		Fmt(R16G16B16A16Snorm, 8);
		Fmt(R16G16B16A16Uscaled, 8);
		Fmt(R16G16B16A16Sscaled, 8);
		Fmt(R16G16B16A16Uint, 8);
		Fmt(R16G16B16A16Sint, 8);
		Fmt(R16G16B16A16Sfloat, 8);
		Fmt(R32Uint, 4);
		Fmt(R32Sint, 4);
		Fmt(R32Sfloat, 4);
		Fmt(R32G32Uint, 8);
		Fmt(R32G32Sint, 8);
		Fmt(R32G32Sfloat, 8);
		Fmt(R32G32B32Uint, 12);
		Fmt(R32G32B32Sint, 12);
		Fmt(R32G32B32Sfloat, 12);
		Fmt(R32G32B32A32Uint, 16);
		Fmt(R32G32B32A32Sint, 16);
		Fmt(R32G32B32A32Sfloat, 16);
		Fmt(R64Uint, 8);
		Fmt(R64Sint, 8);
		Fmt(R64Sfloat, 8);
		Fmt(R64G64Uint, 16);
		Fmt(R64G64Sint, 16);
		Fmt(R64G64Sfloat, 16);
		Fmt(R64G64B64Uint, 24);
		Fmt(R64G64B64Sint, 24);
		Fmt(R64G64B64Sfloat, 24);
		Fmt(R64G64B64A64Uint, 32);
		Fmt(R64G64B64A64Sint, 32);
		Fmt(R64G64B64A64Sfloat, 32);
		Fmt(B10G11R11UfloatPack32, 4);
		Fmt(E5B9G9R9UfloatPack32, 4);
		Fmt(D16Unorm, 2);
		Fmt(X8D24UnormPack32, 4);
		Fmt(D32Sfloat, 4);
		Fmt(S8Uint, 1);
		case vk::Format::eD16UnormS8Uint:
			return aspect == vk::ImageAspectFlagBits::eDepth ? 2 : 1;
		case vk::Format::eD24UnormS8Uint:
		case vk::Format::eD32SfloatS8Uint:
			return aspect == vk::ImageAspectFlagBits::eDepth ? 4 : 1;
			Fmt(Etc2R8G8B8A8UnormBlock, 16);
			Fmt(Etc2R8G8B8A8SrgbBlock, 16);
			Fmt(Etc2R8G8B8A1UnormBlock, 8);
			Fmt(Etc2R8G8B8A1SrgbBlock, 8);
			Fmt(Etc2R8G8B8UnormBlock, 8);
			Fmt(Etc2R8G8B8SrgbBlock, 8);
			Fmt(EacR11UnormBlock, 8);
			Fmt(EacR11SnormBlock, 8);
			Fmt(EacR11G11UnormBlock, 16);
			Fmt(EacR11G11SnormBlock, 16);
			Fmt(Bc1RgbUnormBlock, 8);
			Fmt(Bc1RgbSrgbBlock, 8);
			Fmt(Bc1RgbaUnormBlock, 8);
			Fmt(Bc1RgbaSrgbBlock, 8);
			Fmt(Bc2UnormBlock, 16);
			Fmt(Bc2SrgbBlock, 16);
			Fmt(Bc3UnormBlock, 16);
			Fmt(Bc3SrgbBlock, 16);
			Fmt(Bc4UnormBlock, 8);
			Fmt(Bc4SnormBlock, 8);
			Fmt(Bc5UnormBlock, 16);
			Fmt(Bc5SnormBlock, 16);
			Fmt(Bc6HUfloatBlock, 16);
			Fmt(Bc6HSfloatBlock, 16);
			Fmt(Bc7SrgbBlock, 16);
			Fmt(Bc7UnormBlock, 16);
			Fmt(G8B8G8R8422Unorm, 4);
			Fmt(B8G8R8G8422Unorm, 4);
			Fmt(G8B8R83Plane420Unorm, 1);
			Fmt2(G8B8R82Plane420Unorm, 1, 2);
			Fmt(G8B8R83Plane422Unorm, 1);
			Fmt2(G8B8R82Plane422Unorm, 1, 2);
			Fmt(G8B8R83Plane444Unorm, 1);
			Fmt(R10X6UnormPack16, 2);
			Fmt(R10X6G10X6Unorm2Pack16, 4);
			Fmt(R10X6G10X6B10X6A10X6Unorm4Pack16, 8);
			Fmt(G10X6B10X6G10X6R10X6422Unorm4Pack16, 8);
			Fmt(B10X6G10X6R10X6G10X6422Unorm4Pack16, 8);
			Fmt(G10X6B10X6R10X63Plane420Unorm3Pack16, 2);
			Fmt(G10X6B10X6R10X63Plane422Unorm3Pack16, 2);
			Fmt(G10X6B10X6R10X63Plane444Unorm3Pack16, 2);
			Fmt2(G10X6B10X6R10X62Plane420Unorm3Pack16, 2, 4);
			Fmt2(G10X6B10X6R10X62Plane422Unorm3Pack16, 2, 4);
			Fmt(R12X4UnormPack16, 2);
			Fmt(R12X4G12X4Unorm2Pack16, 4);
			Fmt(R12X4G12X4B12X4A12X4Unorm4Pack16, 8);
			Fmt(G12X4B12X4G12X4R12X4422Unorm4Pack16, 8);
			Fmt(B12X4G12X4R12X4G12X4422Unorm4Pack16, 8);
			Fmt(G12X4B12X4R12X43Plane420Unorm3Pack16, 2);
			Fmt(G12X4B12X4R12X43Plane422Unorm3Pack16, 2);
			Fmt(G12X4B12X4R12X43Plane444Unorm3Pack16, 2);
			Fmt2(G12X4B12X4R12X42Plane420Unorm3Pack16, 2, 4);
			Fmt2(G12X4B12X4R12X42Plane422Unorm3Pack16, 2, 4);
			Fmt(G16B16G16R16422Unorm, 8);
			Fmt(B16G16R16G16422Unorm, 8);
			Fmt(G16B16R163Plane420Unorm, 2);
			Fmt(G16B16R163Plane422Unorm, 2);
			Fmt(G16B16R163Plane444Unorm, 2);
			Fmt2(G16B16R162Plane420Unorm, 2, 4);
			Fmt2(G16B16R162Plane422Unorm, 2, 4);

		default:
			return 0;
	}

#undef AstcFmt
#undef Fmt2
#undef Fmt
}
}  // namespace Vulkan
}  // namespace Luna
