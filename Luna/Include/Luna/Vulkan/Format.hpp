#pragma once

#include <Luna/Vulkan/Common.hpp>

namespace Luna {
namespace Vulkan {
void FormatAlignDim(vk::Format format, uint32_t& width, uint32_t& height);
vk::ImageAspectFlags FormatAspectFlags(vk::Format format);
void FormatBlockCount(vk::Format format, uint32_t& width, uint32_t& height);
int FormatChannelCount(vk::Format format);
bool FormatHasDepth(vk::Format format);
bool FormatHasStencil(vk::Format format);
bool FormatHasDepthOrStencil(vk::Format format);
FormatCompressionType GetFormatCompressionType(vk::Format format);
vk::DeviceSize GetFormatLayerSize(
	vk::Format format, vk::ImageAspectFlags aspect, uint32_t width, uint32_t height, uint32_t depth);
bool IsFormatCompressedHDR(vk::Format format);
bool IsFormatSrgb(vk::Format format);
}  // namespace Vulkan
}  // namespace Luna
