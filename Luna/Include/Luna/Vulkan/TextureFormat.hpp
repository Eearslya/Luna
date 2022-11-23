#pragma once

#include <Luna/Vulkan/Common.hpp>

namespace Luna {
namespace Vulkan {
class TextureFormatLayout {
 public:
	struct MipInfo {
		size_t Offset   = 0;
		uint32_t Width  = 1;
		uint32_t Height = 1;
		uint32_t Depth  = 1;

		uint32_t BlockImageHeight = 0;
		uint32_t BlockRowLength   = 0;
		uint32_t ImageHeight      = 0;
		uint32_t RowLength        = 0;
	};

	uint32_t GetArrayLayers() const {
		return _arrayLayers;
	}
	uint32_t GetBlockDimX() const {
		return _blockDimX;
	}
	uint32_t GetBlockDimY() const {
		return _blockDimY;
	}
	uint32_t GetBlockStride() const {
		return _blockStride;
	}
	uint32_t GetDepth(uint32_t mip = 0) const {
		return _mipInfos[mip].Depth;
	}
	vk::Format GetFormat() const {
		return _format;
	}
	uint32_t GetHeight(uint32_t mip = 0) const {
		return _mipInfos[mip].Height;
	}
	size_t GetLayerSize(uint32_t mip) const {
		return _mipInfos[mip].BlockImageHeight * GetRowSize(mip);
	}
	const MipInfo& GetMipInfo(uint32_t mip) const {
		return _mipInfos[mip];
	}
	uint32_t GetMipLevels() const {
		return _mipLevels;
	}
	vk::ImageType GetImageType() const {
		return _imageType;
	}
	size_t GetRequiredSize() const {
		return _requiredSize;
	}
	size_t GetRowSize(uint32_t mip) const {
		return _mipInfos[mip].BlockRowLength * _blockStride;
	}
	uint32_t GetWidth(uint32_t mip = 0) const {
		return _mipInfos[mip].Width;
	}
	size_t LayerByteStride(uint32_t imageHeight, size_t rowByteStride) const {
		return ((imageHeight + _blockDimY - 1) / _blockDimY) * rowByteStride;
	}
	size_t RowByteStride(uint32_t rowLength) const {
		return ((rowLength + _blockDimX - 1) / _blockDimX) * _blockStride;
	}

	void* GetBuffer() {
		return _buffer;
	}

	std::vector<vk::BufferImageCopy> BuildBufferImageCopies() const;
	void Set1D(vk::Format format, uint32_t width, uint32_t arrayLayers = 1, uint32_t mipLevels = 1);
	void Set2D(vk::Format format, uint32_t width, uint32_t height, uint32_t arrayLayers = 1, uint32_t mipLevels = 1);
	void Set3D(vk::Format format, uint32_t width, uint32_t height, uint32_t depth, uint32_t mipLevels = 1);
	void SetBuffer(size_t size, void* buffer);

	void* Data(uint32_t layer = 0, uint32_t mip = 0) const {
		const auto& mipInfo = _mipInfos[mip];

		return _buffer + mipInfo.Offset + _blockStride * layer * mipInfo.BlockRowLength * mipInfo.BlockImageHeight;
	}
	template <typename T>
	T* DataGeneric(uint32_t x = 0, uint32_t y = 0, uint32_t sliceIndex = 0, uint32_t mip = 0) const {
		const auto& mipInfo = _mipInfos[mip];

		return reinterpret_cast<T*>(_buffer + mipInfo.Offset) +
		       (sliceIndex * mipInfo.BlockRowLength * mipInfo.BlockImageHeight) + (y * mipInfo.BlockRowLength) + x;
	}
	void* DataOpaque(uint32_t x, uint32_t y, uint32_t sliceIndex, uint32_t mip = 0) const {
		return DataGeneric<uint8_t>(x, y, sliceIndex, mip);
	}
	template <typename T>
	T* Data1D(uint32_t x, uint32_t layer = 0, uint32_t mip = 0) const {
		return DataGeneric<T>(x, 0, layer, mip);
	}
	template <typename T>
	T* Data2D(uint32_t x, uint32_t y, uint32_t layer = 0, uint32_t mip = 0) const {
		return DataGeneric<T>(x, y, layer, mip);
	}
	template <typename T>
	T* Data3D(uint32_t x, uint32_t y, uint32_t z, uint32_t mip = 0) const {
		return DataGeneric<T>(x, y, z, mip);
	}

	static void FormatBlockDim(vk::Format format, uint32_t& width, uint32_t& height);
	static uint32_t FormatBlockSize(vk::Format format, vk::ImageAspectFlags aspect);

 private:
	void FillMipInfo(uint32_t width, uint32_t height, uint32_t depth);

	uint8_t* _buffer   = nullptr;
	size_t _bufferSize = 0;

	vk::Format _format       = vk::Format::eUndefined;
	vk::ImageType _imageType = vk::ImageType::e2D;
	size_t _requiredSize     = 0;

	uint32_t _arrayLayers = 1;
	uint32_t _blockDimX   = 1;
	uint32_t _blockDimY   = 1;
	uint32_t _blockStride = 1;
	uint32_t _mipLevels   = 1;

	std::array<MipInfo, 16> _mipInfos;
};
}  // namespace Vulkan
}  // namespace Luna
