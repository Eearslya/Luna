#pragma once

#include <Luna/Math/Vec2.hpp>
#include <Luna/Vulkan/Common.hpp>
#include <Luna/Vulkan/Format.hpp>

namespace Luna {
namespace Vulkan {
struct MipInfo final {
	size_t Offset = 0;
	vk::Extent3D Extent;

	uint32_t BlockImageHeight = 0;
	uint32_t BlockRowLength   = 0;
	uint32_t ImageHeight      = 0;
	uint32_t RowLength        = 0;
};

class FormatLayout final {
 public:
	FormatLayout(vk::Format format, uint32_t width, uint32_t arrayLayers = 1, uint32_t mipLevels = 1);
	FormatLayout(vk::Format format, const vk::Extent2D& extent, uint32_t arrayLayers = 1, uint32_t mipLevels = 1);
	FormatLayout(vk::Format format, const vk::Extent3D& extent, uint32_t mipLevels = 1);

	std::vector<vk::BufferImageCopy> BuildBufferImageCopies() const;
	Vec2ui FormatBlockDims() const;
	uint32_t FormatBlockSize(vk::ImageAspectFlags aspect) const;
	size_t LayerByteStride(uint32_t imageHeight, size_t rowByteStride) const;
	size_t RowByteStride(uint32_t rowLength) const;

	uint32_t GetArrayLayers() const {
		return _arrayLayers;
	}
	Vec2ui GetBlockDims() const {
		return _blockDims;
	}
	uint32_t GetBlockStride() const {
		return _blockStride;
	}
	void* GetBuffer() const {
		return _buffer;
	}
	vk::Extent3D GetExtent(uint32_t mip = 0) const;
	vk::Format GetFormat() const {
		return _format;
	}
	vk::ImageType GetImageType() const {
		return _imageType;
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

	void SetBuffer(void* buffer, size_t size);

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
	T* DataGeneric() const {
		return DataGeneric<T>(0, 0, 0, 0);
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

 private:
	void FillMipInfo();

	uint8_t* _buffer   = nullptr;
	size_t _bufferSize = 0;
	vk::Extent3D _extent;
	vk::Format _format       = vk::Format::eUndefined;
	vk::ImageType _imageType = vk::ImageType::e2D;
	size_t _requiredSize     = 0;

	uint32_t _arrayLayers = 1;
	Vec2ui _blockDims;
	uint32_t _blockStride = 1;
	uint32_t _mipLevels   = 1;

	std::array<MipInfo, 16> _mips;
};
}  // namespace Vulkan
}  // namespace Luna
