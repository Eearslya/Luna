#pragma once

#include <array>
#include <vector>

#include "Common.hpp"

namespace Luna {
namespace Vulkan {
class TextureFormatLayout {
 public:
	struct MipInfo {
		size_t Offset             = 0;
		uint32_t Width            = 1;
		uint32_t Height           = 1;
		uint32_t Depth            = 1;
		uint32_t BlockImageHeight = 0;
		uint32_t BlockRowLength   = 0;
		uint32_t ImageHeight      = 0;
		uint32_t RowLength        = 0;
	};

	void* Data(uint32_t layer = 0, uint32_t mip = 0) const {
		auto& mipInfo  = _mips[mip];
		uint8_t* slice = _buffer + mipInfo.Offset;
		slice += _blockStride * layer * mipInfo.BlockRowLength * mipInfo.BlockImageHeight;
		return slice;
	}
	void* DataOpaque(uint32_t x, uint32_t y, uint32_t sliceIndex, uint32_t mip = 0) const {
		auto& mipInfo  = _mips[mip];
		uint8_t* slice = _buffer + mipInfo.Offset;
		size_t offset  = sliceIndex * mipInfo.BlockRowLength * mipInfo.BlockImageHeight;
		offset += y * mipInfo.BlockRowLength;
		offset += x;
		return slice + offset * _blockStride;
	}

	template <typename T>
	T* DataGeneric(uint32_t x, uint32_t y, uint32_t sliceIndex, uint32_t mip = 0) const {
		auto& mipInfo = _mips[mip];
		T* slice      = reinterpret_cast<T*>(_buffer + mipInfo.Offset);
		slice += sliceIndex * mipInfo.BlockRowLength * mipInfo.BlockImageHeight;
		slice += y * mipInfo.BlockRowLength;
		slice += x;
		return slice;
	}

	template <typename T>
	T* DataGeneric() const {
		return DataGeneric<T>(0, 0, 0, 0);
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
	void* GetBuffer() const {
		return _buffer;
	}
	uint32_t GetDepth(uint32_t mip = 0) const {
		return _mips[mip].Depth;
	}
	vk::Format GetFormat() const {
		return _format;
	}
	uint32_t GetHeight(uint32_t mip = 0) const {
		return _mips[mip].Height;
	}
	vk::ImageType GetImageType() const {
		return _imageType;
	}
	size_t GetLayerSize(uint32_t mip) const {
		return _mips[mip].BlockImageHeight * GetRowSize(mip);
	}
	const MipInfo& GetMipInfo(uint32_t mip) const {
		return _mips[mip];
	}
	uint32_t GetMipLevels() const {
		return _mipLevels;
	}
	size_t GetRequiredSize() const {
		return _requiredSize;
	}
	size_t GetRowSize(uint32_t mip) const {
		return _mips[mip].BlockRowLength * _blockStride;
	}
	uint32_t GetWidth(uint32_t mip = 0) const {
		return _mips[mip].Width;
	}
	size_t LayerByteStride(uint32_t imageHeight, size_t rowByteStride) const {
		return ((imageHeight + _blockDimY - 1) / _blockDimY) * rowByteStride;
	}
	size_t RowByteStride(uint32_t rowLength) const {
		return ((rowLength + _blockDimX - 1) / _blockDimX) * _blockStride;
	}

	std::vector<vk::BufferImageCopy> BuildBufferImageCopies() const;
	void Set1D(vk::Format format, uint32_t width, uint32_t arrayLayers = 1, uint32_t mipLevels = 1);
	void Set2D(vk::Format format, uint32_t width, uint32_t height, uint32_t arrayLayers = 1, uint32_t mipLevels = 1);
	void Set3D(vk::Format format, uint32_t width, uint32_t height, uint32_t depth, uint32_t mipLevels = 1);
	void SetBuffer(void* buffer, size_t size);

	static void FormatBlockDim(vk::Format format, uint32_t& width, uint32_t& height);
	static uint32_t FormatBlockSize(vk::Format format, vk::ImageAspectFlags aspect);
	static uint32_t MipLevels(uint32_t width, uint32_t height = 1, uint32_t depth = 1);

 private:
	void FillMipInfo(uint32_t width, uint32_t height, uint32_t depth);

	uint8_t* _buffer   = nullptr;
	size_t _bufferSize = 0;

	vk::Format _format;
	vk::ImageType _imageType;
	size_t _requiredSize = 0;

	uint32_t _arrayLayers = 1;
	uint32_t _blockDimX   = 1;
	uint32_t _blockDimY   = 1;
	uint32_t _blockStride = 1;
	uint32_t _mipLevels   = 1;

	std::array<MipInfo, 16> _mips;
};
}  // namespace Vulkan
}  // namespace Luna
