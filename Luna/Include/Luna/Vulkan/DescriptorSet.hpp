#pragma once

#include <Luna/Vulkan/Common.hpp>

namespace Luna {
namespace Vulkan {
struct DescriptorSetLayout {
	uint8_t ArraySizes[MaxDescriptorBindings] = {};
	uint32_t FloatMask                        = 0;
	uint32_t ImmutableSamplerMask             = 0;
	uint32_t InputAttachmentMask              = 0;
	uint32_t SampledBufferMask                = 0;
	uint32_t SampledImageMask                 = 0;
	uint32_t SamplerMask                      = 0;
	uint32_t SeparateImageMask                = 0;
	uint32_t StorageBufferMask                = 0;
	uint32_t StorageImageMask                 = 0;
	uint32_t UniformBufferMask                = 0;

	constexpr static const uint8_t UnsizedArray = 0xff;
};
}  // namespace Vulkan
}  // namespace Luna
