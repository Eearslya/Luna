#pragma once

#include <Luna/Utility/EnumClass.hpp>
#include <Luna/Vulkan/Common.hpp>

namespace Luna {
namespace Vulkan {
static inline uint32_t CalculateMipLevels(const vk::Extent3D& extent) {
	uint32_t longest = std::max(std::max(extent.width, extent.height), extent.depth);

	return static_cast<uint32_t>(std::floor(std::log2(longest))) + 1;
}

enum class ImageCreateFlagBits { GenerateMipmaps = 1 << 0 };
using ImageCreateFlags = Bitmask<ImageCreateFlagBits>;

enum class ImageDomain { Physical, Transient };

struct ImageCreateInfo {
	static ImageCreateInfo Immutable2D(vk::Format format, const vk::Extent2D& extent, bool mipmapped = false) {
		return {.Format        = format,
		        .Type          = vk::ImageType::e2D,
		        .Usage         = vk::ImageUsageFlagBits::eSampled,
		        .Extent        = vk::Extent3D(extent.width, extent.height, 1),
		        .MipLevels     = mipmapped ? 0u : 1u,
		        .InitialLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
		        .Flags         = mipmapped ? ImageCreateFlagBits::GenerateMipmaps : ImageCreateFlags()};
	}

	ImageDomain Domain              = ImageDomain::Physical;
	vk::Format Format               = vk::Format::eUndefined;
	vk::ImageType Type              = vk::ImageType::e2D;
	vk::ImageUsageFlags Usage       = {};
	vk::Extent3D Extent             = {};
	uint32_t ArrayLayers            = 1;
	uint32_t MipLevels              = 1;
	vk::SampleCountFlagBits Samples = vk::SampleCountFlagBits::e1;
	vk::ImageLayout InitialLayout   = vk::ImageLayout::eUndefined;
	ImageCreateFlags Flags          = {};
};

struct ImageDeleter {
	void operator()(Image* image);
};

class Image : public IntrusivePtrEnabled<Image, ImageDeleter, HandleCounter>,
							public Cookie,
							public InternalSyncEnabled {
	friend class ObjectPool<Image>;
	friend struct ImageDeleter;

 public:
	~Image() noexcept;

 private:
	Image(Device& device, const ImageCreateInfo& createInfo);

	Device& _device;
	ImageCreateInfo _createInfo;
	vk::Image _image;
	VmaAllocation _allocation;
};
}  // namespace Vulkan

template <>
struct EnableBitmaskOperators<Vulkan::ImageCreateFlagBits> : std::true_type {};
}  // namespace Luna
