#pragma once

#include <Luna/Utility/EnumClass.hpp>

namespace Luna {
namespace Vulkan {
template <typename T>
static const char* VulkanEnumToString(const T value) {
	return nullptr;
}

enum class BufferCreateFlagBits { ZeroInitialize = 1 << 0 };
using BufferCreateFlags = Bitmask<BufferCreateFlagBits>;

enum class FormatCompressionType { Uncompressed, BC, ETC, ASTC };
constexpr static const int FormatCompressionTypeCount = 4;
template <>
const char* VulkanEnumToString<FormatCompressionType>(const FormatCompressionType value) {
	switch (value) {
		case FormatCompressionType::Uncompressed:
			return "Uncompressed";
		case FormatCompressionType::BC:
			return "BC";
		case FormatCompressionType::ETC:
			return "ETC";
		case FormatCompressionType::ASTC:
			return "ASTC";
	}

	return "Unknown";
}

enum class ImageDomain { Physical, Transient };

enum class ImageLayout { Optimal, General };

enum class QueueType { Graphics, Transfer, Compute };
constexpr static const int QueueTypeCount = 3;
template <>
const char* VulkanEnumToString<QueueType>(const QueueType value) {
	switch (value) {
		case QueueType::Graphics:
			return "Graphics";
		case QueueType::Transfer:
			return "Transfer";
		case QueueType::Compute:
			return "Compute";
	}

	return "Unknown";
}

enum class CommandBufferType {
	Generic       = static_cast<int>(QueueType::Graphics),
	AsyncCompute  = static_cast<int>(QueueType::Compute),
	AsyncTransfer = static_cast<int>(QueueType::Transfer),
	AsyncGraphics = QueueTypeCount
};

enum class ImageCreateFlagBits {
	GenerateMipmaps              = 1 << 0,
	ForceArray                   = 1 << 1,
	MutableSrgb                  = 1 << 2,
	ConcurrentQueueGraphics      = 1 << 3,
	ConcurrentQueueAsyncCompute  = 1 << 4,
	ConcurrentQueueAsyncGraphics = 1 << 5,
	ConcurrentQueueAsyncTransfer = 1 << 6
};
using ImageCreateFlags = Bitmask<ImageCreateFlagBits>;

enum class ImageViewCreateFlagBits { ForceArray = 1 << 0 };
using ImageViewCreateFlags = Bitmask<ImageViewCreateFlagBits>;
}  // namespace Vulkan
}  // namespace Luna

template <>
struct Luna::EnableBitmaskOperators<Luna::Vulkan::BufferCreateFlagBits> : std::true_type {};
template <>
struct Luna::EnableBitmaskOperators<Luna::Vulkan::ImageCreateFlagBits> : std::true_type {};
template <>
struct Luna::EnableBitmaskOperators<Luna::Vulkan::ImageViewCreateFlagBits> : std::true_type {};
