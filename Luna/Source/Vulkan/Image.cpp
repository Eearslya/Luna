#include <Luna/Core/Log.hpp>
#include <Luna/Vulkan/Device.hpp>
#include <Luna/Vulkan/Image.hpp>

namespace Luna {
namespace Vulkan {
void ImageDeleter::operator()(Image* image) {
	image->_device.DestroyImage({}, image);
}

void ImageViewDeleter::operator()(ImageView* view) {
	view->_device.DestroyImageView({}, view);
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

	_image       = image;
	_accessFlags = ImageUsageToAccess(imageCI.usage);
	_stageFlags  = ImageUsageToStages(imageCI.usage);
}

Image::Image(Device& device, const ImageCreateInfo& createInfo, vk::Image image)
		: Cookie(device), _device(device), _image(image), _createInfo(createInfo), _ownsImage(false) {}

Image::~Image() noexcept {
	auto dev = _device.GetDevice();

	if (_ownsImage) {
		if (_image) { dev.destroyImage(_image); }
		if (_allocation) { vmaFreeMemory(_device.GetAllocator(), _allocation); }
	}
}

vk::ImageLayout Image::GetLayout(vk::ImageLayout optimal) const {
	return _layoutType == ImageLayoutType::Optimal ? optimal : vk::ImageLayout::eGeneral;
}

ImageView::ImageView(Device& device, const ImageViewCreateInfo& createInfo)
		: Cookie(device), _device(device), _createInfo(createInfo) {
	Log::Trace("[Vulkan::ImageView] Creating new ImageView.");

	const auto& imageCI = _createInfo.Image->GetCreateInfo();

	const vk::ImageViewCreateInfo viewCI({},
	                                     _createInfo.Image->GetImage(),
	                                     _createInfo.Type,
	                                     _createInfo.Format,
	                                     vk::ComponentMapping(),
	                                     vk::ImageSubresourceRange(FormatToAspect(_createInfo.Format),
	                                                               _createInfo.BaseMipLevel,
	                                                               _createInfo.MipLevels,
	                                                               _createInfo.BaseArrayLayer,
	                                                               _createInfo.ArrayLayers));
	_imageView = _device.GetDevice().createImageView(viewCI);
}

ImageView::~ImageView() noexcept {
	if (_imageView) { _device.GetDevice().destroyImageView(_imageView); }
}

vk::ImageView ImageView::GetRenderTargetView(uint32_t layer) const {
	if (_createInfo.Image->GetCreateInfo().Domain == ImageDomain::Transient) { return _imageView; }

	if (_renderTargetViews.empty()) {
		return _imageView;
	} else {
		return _renderTargetViews[layer];
	}
}
}  // namespace Vulkan
}  // namespace Luna
