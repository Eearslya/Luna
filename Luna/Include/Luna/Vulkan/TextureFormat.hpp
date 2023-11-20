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

	[[nodiscard]] uint32_t GetArrayLayers() const noexcept {
		return _arrayLayers;
	}
	[[nodiscard]] uint32_t GetBlockDimX() const noexcept {
		return _blockDimX;
	}
	[[nodiscard]] uint32_t GetBlockDimY() const noexcept {
		return _blockDimY;
	}
	[[nodiscard]] uint32_t GetBlockStride() const noexcept {
		return _blockStride;
	}
	[[nodiscard]] uint32_t GetDepth(uint32_t mip = 0) const noexcept {
		return _mipInfos[mip].Depth;
	}
	[[nodiscard]] vk::Format GetFormat() const noexcept {
		return _format;
	}
	[[nodiscard]] uint32_t GetHeight(uint32_t mip = 0) const noexcept {
		return _mipInfos[mip].Height;
	}
	[[nodiscard]] size_t GetLayerSize(uint32_t mip) const noexcept {
		return _mipInfos[mip].BlockImageHeight * GetRowSize(mip);
	}
	[[nodiscard]] const MipInfo& GetMipInfo(uint32_t mip) const noexcept {
		return _mipInfos[mip];
	}
	[[nodiscard]] uint32_t GetMipLevels() const noexcept {
		return _mipLevels;
	}
	[[nodiscard]] vk::ImageType GetImageType() const noexcept {
		return _imageType;
	}
	[[nodiscard]] size_t GetRequiredSize() const noexcept {
		return _requiredSize;
	}
	[[nodiscard]] size_t GetRowSize(uint32_t mip) const noexcept {
		return _mipInfos[mip].BlockRowLength * _blockStride;
	}
	[[nodiscard]] uint32_t GetWidth(uint32_t mip = 0) const noexcept {
		return _mipInfos[mip].Width;
	}
	[[nodiscard]] size_t LayerByteStride(uint32_t imageHeight, size_t rowByteStride) const noexcept {
		return ((imageHeight + _blockDimY - 1) / _blockDimY) * rowByteStride;
	}
	[[nodiscard]] size_t RowByteStride(uint32_t rowLength) const noexcept {
		return ((rowLength + _blockDimX - 1) / _blockDimX) * _blockStride;
	}

	[[nodiscard]] void* GetBuffer() noexcept {
		return _buffer;
	}

	[[nodiscard]] std::vector<vk::BufferImageCopy> BuildBufferImageCopies() const;
	void Set1D(vk::Format format, uint32_t width, uint32_t arrayLayers = 1, uint32_t mipLevels = 1);
	void Set2D(vk::Format format, uint32_t width, uint32_t height, uint32_t arrayLayers = 1, uint32_t mipLevels = 1);
	void Set3D(vk::Format format, uint32_t width, uint32_t height, uint32_t depth, uint32_t mipLevels = 1);
	void SetBuffer(size_t size, void* buffer);

	[[nodiscard]] void* Data(uint32_t layer = 0, uint32_t mip = 0) const {
		const auto& mipInfo = _mipInfos[mip];

		return _buffer + mipInfo.Offset + _blockStride * layer * mipInfo.BlockRowLength * mipInfo.BlockImageHeight;
	}
	template <typename T>
	[[nodiscard]] T* DataGeneric(uint32_t x = 0, uint32_t y = 0, uint32_t sliceIndex = 0, uint32_t mip = 0) const {
		const auto& mipInfo = _mipInfos[mip];

		return reinterpret_cast<T*>(_buffer + mipInfo.Offset) +
		       (sliceIndex * mipInfo.BlockRowLength * mipInfo.BlockImageHeight) + (y * mipInfo.BlockRowLength) + x;
	}
	[[nodiscard]] void* DataOpaque(uint32_t x, uint32_t y, uint32_t sliceIndex, uint32_t mip = 0) const {
		return DataGeneric<uint8_t>(x, y, sliceIndex, mip);
	}
	template <typename T>
	[[nodiscard]] T* Data1D(uint32_t x, uint32_t layer = 0, uint32_t mip = 0) const {
		return DataGeneric<T>(x, 0, layer, mip);
	}
	template <typename T>
	[[nodiscard]] T* Data2D(uint32_t x, uint32_t y, uint32_t layer = 0, uint32_t mip = 0) const {
		return DataGeneric<T>(x, y, layer, mip);
	}
	template <typename T>
	[[nodiscard]] T* Data3D(uint32_t x, uint32_t y, uint32_t z, uint32_t mip = 0) const {
		return DataGeneric<T>(x, y, z, mip);
	}

	static void FormatBlockDim(vk::Format format, uint32_t& width, uint32_t& height);
	[[nodiscard]] static uint32_t FormatBlockSize(vk::Format format, vk::ImageAspectFlags aspect);

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
