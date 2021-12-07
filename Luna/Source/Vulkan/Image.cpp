#include <Luna/Core/Log.hpp>
#include <Luna/Vulkan/Device.hpp>
#include <Luna/Vulkan/Image.hpp>

namespace Luna {
namespace Vulkan {
void ImageDeleter::operator()(Image* image) {
	image->_device.DestroyImage({}, image);
}

Image::Image(Device& device, const ImageCreateInfo& createInfo)
		: Cookie(device), _device(device), _createInfo(createInfo) {
	Log::Trace("[Vulkan::Image] Creating new Image.");

	const vk::ImageCreateInfo imageCI({},
	                                  createInfo.Type,
	                                  createInfo.Format,
	                                  createInfo.Extent,
	                                  createInfo.MipLevels,
	                                  createInfo.ArrayLayers,
	                                  createInfo.Samples,
	                                  vk::ImageTiling::eOptimal,
	                                  createInfo.Usage,
	                                  vk::SharingMode::eExclusive,
	                                  nullptr,
	                                  vk::ImageLayout::eUndefined);
	VmaAllocationCreateInfo imageAI{.usage = VMA_MEMORY_USAGE_GPU_ONLY};
	if (createInfo.Usage & vk::ImageUsageFlagBits::eTransientAttachment) {
		imageAI.preferredFlags |= VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT;
	}

	VkImage image;
	VmaAllocationInfo allocationInfo;
	const vk::Result createResult =
		static_cast<vk::Result>(vmaCreateImage(_device.GetAllocator(),
	                                         reinterpret_cast<const VkImageCreateInfo*>(&imageCI),
	                                         &imageAI,
	                                         &image,
	                                         &_allocation,
	                                         &allocationInfo));
	if (createResult != vk::Result::eSuccess) {
		Log::Error("[Vulkan::Error] Error creating image: {}", vk::to_string(createResult));
		// Use vulkan.hpp's ResultValue to throw the proper exception.
		vk::createResultValue(createResult, "vmaCreateImage");
	}

	_image = image;
}

Image::~Image() noexcept {
	auto dev = _device.GetDevice();

	if (_image) { dev.destroyImage(_image); }
	if (_allocation) { vmaFreeMemory(_device.GetAllocator(), _allocation); }
}

vk::ImageLayout Image::GetLayout(vk::ImageLayout optimal) const {
	return _layoutType == ImageLayoutType::Optimal ? optimal : vk::ImageLayout::eGeneral;
}
}  // namespace Vulkan
}  // namespace Luna
