#include "Image.hpp"

#include "Device.hpp"

namespace Luna {
namespace Vulkan {
void ImageViewDeleter::operator()(ImageView* view) {
	view->_device._imageViewPool.Free(view);
}

ImageView::ImageView(Device& device, vk::ImageView view, const ImageViewCreateInfo& viewCI)
		: Cookie(device), _device(device), _view(view), _createInfo(viewCI) {}

ImageView::~ImageView() noexcept {
	if (_internalSync) {
		_device.DestroyImageViewNoLock(_view);
		if (_depthView) { _device.DestroyImageViewNoLock(_depthView); }
		if (_stencilView) { _device.DestroyImageViewNoLock(_stencilView); }
		if (_srgbView) { _device.DestroyImageViewNoLock(_srgbView); }
		if (_unormView) { _device.DestroyImageViewNoLock(_unormView); }
		for (auto& view : _renderTargetViews) { _device.DestroyImageViewNoLock(view); }
	} else {
		_device.DestroyImageView(_view);
		if (_depthView) { _device.DestroyImageView(_depthView); }
		if (_stencilView) { _device.DestroyImageView(_stencilView); }
		if (_srgbView) { _device.DestroyImageView(_srgbView); }
		if (_unormView) { _device.DestroyImageView(_unormView); }
		for (auto& view : _renderTargetViews) { _device.DestroyImageView(view); }
	}
}

vk::ImageView ImageView::GetRenderTargetView(uint32_t layer) const {
	if (_createInfo.Image->GetCreateInfo().Domain == ImageDomain::Transient) { return _view; }

	if (_renderTargetViews.empty()) {
		return _view;
	} else {
		return _renderTargetViews[layer];
	}
}

vk::ImageViewType ImageCreateInfo::GetImageViewType() const {
	uint32_t layers    = ArrayLayers;
	uint32_t baseLayer = 0;
	bool forceArray(MiscFlags & ImageCreateFlagBits::ForceArray);

	switch (Type) {
		case vk::ImageType::e1D:
			return (layers > 1 || forceArray) ? vk::ImageViewType::e1DArray : vk::ImageViewType::e1D;
			break;

		case vk::ImageType::e2D:
			if ((Flags & vk::ImageCreateFlagBits::eCubeCompatible) && (layers % 6) == 0) {
				return (layers > 6 || forceArray) ? vk::ImageViewType::eCubeArray : vk::ImageViewType::eCube;
			} else {
				return (layers > 1 || forceArray) ? vk::ImageViewType::e2DArray : vk::ImageViewType::e2D;
			}
			break;

		case vk::ImageType::e3D:
			return vk::ImageViewType::e3D;

		default:
			return {};
	}
}

void ImageDeleter::operator()(Image* image) {
	image->_device._imagePool.Free(image);
}

Image::Image(Device& device,
             vk::Image image,
             vk::ImageView defaultView,
             const VmaAllocation& allocation,
             const ImageCreateInfo& imageCI,
             vk::ImageViewType viewType)
		: Cookie(device), _device(device), _image(image), _allocation(allocation), _createInfo(imageCI) {
	_accessFlags = ImageUsageToAccess(_createInfo.Usage);
	_stageFlags  = ImageUsageToStages(_createInfo.Usage);

	if (defaultView) {
		const ImageViewCreateInfo viewCI{.Image          = this,
		                                 .Format         = _createInfo.Format,
		                                 .BaseMipLevel   = 0,
		                                 .MipLevels      = _createInfo.MipLevels,
		                                 .BaseArrayLayer = 0,
		                                 .ArrayLayers    = _createInfo.ArrayLayers,
		                                 .Type           = viewType};
		_view = ImageViewHandle(_device._imageViewPool.Allocate(_device, defaultView, viewCI));
	}
}

Image::~Image() noexcept {
	if (_imageOwned) {
		if (_internalSync) {
			_device.DestroyImageNoLock(_image);
		} else {
			_device.DestroyImage(_image);
		}
	}

	if (_memoryOwned && _allocation) {
		if (_internalSync) {
			_device.FreeMemoryNoLock(_allocation);
		} else {
			_device.FreeMemory(_allocation);
		}
	}
}
}  // namespace Vulkan
}  // namespace Luna
