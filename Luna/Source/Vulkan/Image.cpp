#include <Luna/Vulkan/Device.hpp>
#include <Luna/Vulkan/Image.hpp>

namespace Luna {
namespace Vulkan {
vk::AccessFlags ImageLayoutToAccess(vk::ImageLayout layout) {
	switch (layout) {
		case vk::ImageLayout::eColorAttachmentOptimal:
			return vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite;

		case vk::ImageLayout::eShaderReadOnlyOptimal:
			return vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eInputAttachmentRead;

		case vk::ImageLayout::eDepthReadOnlyStencilAttachmentOptimal:
			return vk::AccessFlagBits::eDepthStencilAttachmentRead | vk::AccessFlagBits::eDepthStencilAttachmentWrite;

		case vk::ImageLayout::eDepthAttachmentStencilReadOnlyOptimal:
			return vk::AccessFlagBits::eInputAttachmentRead | vk::AccessFlagBits::eDepthStencilAttachmentRead;

		case vk::ImageLayout::eTransferSrcOptimal:
			return vk::AccessFlagBits::eTransferRead;

		case vk::ImageLayout::eTransferDstOptimal:
			return vk::AccessFlagBits::eTransferWrite;

		default:
			// When unknown, simply return all access bits, to be safe.
			return static_cast<vk::AccessFlagBits>(~0u);
	}
}

vk::AccessFlags2 ImageLayoutToAccess2(vk::ImageLayout layout) {
	switch (layout) {
		case vk::ImageLayout::eColorAttachmentOptimal:
			return vk::AccessFlagBits2::eColorAttachmentRead | vk::AccessFlagBits2::eColorAttachmentWrite;

		case vk::ImageLayout::eShaderReadOnlyOptimal:
			return vk::AccessFlagBits2::eShaderSampledRead | vk::AccessFlagBits2::eInputAttachmentRead;

		case vk::ImageLayout::eDepthStencilAttachmentOptimal:
			return vk::AccessFlagBits2::eDepthStencilAttachmentRead | vk::AccessFlagBits2::eDepthStencilAttachmentWrite;

		case vk::ImageLayout::eDepthAttachmentStencilReadOnlyOptimal:
			return vk::AccessFlagBits2::eInputAttachmentRead | vk::AccessFlagBits2::eDepthStencilAttachmentRead;

		case vk::ImageLayout::eTransferSrcOptimal:
			return vk::AccessFlagBits2::eTransferRead;

		case vk::ImageLayout::eTransferDstOptimal:
			return vk::AccessFlagBits2::eTransferWrite;

		default:
			// When unknown, simply return all access bits, to be safe.
			return static_cast<vk::AccessFlagBits2>(~0u);
	}
}

vk::AccessFlags ImageUsageToAccess(vk::ImageUsageFlags usage) {
	vk::AccessFlags access = {};

	if (usage & (vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc)) {
		access |= vk::AccessFlagBits::eTransferRead | vk::AccessFlagBits::eTransferWrite;
	}
	if (usage & vk::ImageUsageFlagBits::eSampled) { access |= vk::AccessFlagBits::eShaderRead; }
	if (usage & vk::ImageUsageFlagBits::eStorage) {
		access |= vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite;
	}
	if (usage & vk::ImageUsageFlagBits::eColorAttachment) {
		access |= vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite;
	}
	if (usage & vk::ImageUsageFlagBits::eDepthStencilAttachment) {
		access |= vk::AccessFlagBits::eDepthStencilAttachmentRead | vk::AccessFlagBits::eDepthStencilAttachmentWrite;
	}
	if (usage & vk::ImageUsageFlagBits::eInputAttachment) { access |= vk::AccessFlagBits::eInputAttachmentRead; }

	if (usage & vk::ImageUsageFlagBits::eTransientAttachment) {
		access &= vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite |
		          vk::AccessFlagBits::eDepthStencilAttachmentRead | vk::AccessFlagBits::eDepthStencilAttachmentWrite |
		          vk::AccessFlagBits::eInputAttachmentRead;
	}

	return access;
}

vk::AccessFlags2 ImageUsageToAccess2(vk::ImageUsageFlags usage) {}

vk::FormatFeatureFlags ImageUsageToFeatures(vk::ImageUsageFlags usage) {
	vk::FormatFeatureFlags features = {};

	if (usage & vk::ImageUsageFlagBits::eSampled) { features |= vk::FormatFeatureFlagBits::eSampledImage; }
	if (usage & vk::ImageUsageFlagBits::eStorage) { features |= vk::FormatFeatureFlagBits::eStorageImage; }
	if (usage & vk::ImageUsageFlagBits::eColorAttachment) { features |= vk::FormatFeatureFlagBits::eColorAttachment; }
	if (usage & vk::ImageUsageFlagBits::eDepthStencilAttachment) {
		features |= vk::FormatFeatureFlagBits::eDepthStencilAttachment;
	}

	return features;
}

vk::PipelineStageFlags ImageUsageToStages(vk::ImageUsageFlags usage) {
	vk::PipelineStageFlags stages = {};

	if (usage & (vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc)) {
		stages |= vk::PipelineStageFlagBits::eTransfer;
	}
	if (usage & vk::ImageUsageFlagBits::eSampled) {
		stages |= vk::PipelineStageFlagBits::eComputeShader | vk::PipelineStageFlagBits::eFragmentShader |
		          vk::PipelineStageFlagBits::eVertexShader | vk::PipelineStageFlagBits::eTessellationControlShader |
		          vk::PipelineStageFlagBits::eTessellationEvaluationShader;
	}
	if (usage & vk::ImageUsageFlagBits::eStorage) { stages |= vk::PipelineStageFlagBits::eComputeShader; }
	if (usage & vk::ImageUsageFlagBits::eColorAttachment) { stages |= vk::PipelineStageFlagBits::eColorAttachmentOutput; }
	if (usage & vk::ImageUsageFlagBits::eDepthStencilAttachment) {
		stages |= vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests;
	}
	if (usage & vk::ImageUsageFlagBits::eInputAttachment) { stages |= vk::PipelineStageFlagBits::eFragmentShader; }

	if (usage & vk::ImageUsageFlagBits::eTransientAttachment) {
		vk::PipelineStageFlags possible = vk::PipelineStageFlagBits::eColorAttachmentOutput |
		                                  vk::PipelineStageFlagBits::eEarlyFragmentTests |
		                                  vk::PipelineStageFlagBits::eLateFragmentTests;
		if (usage & vk::ImageUsageFlagBits::eInputAttachment) { possible |= vk::PipelineStageFlagBits::eFragmentShader; }

		stages &= possible;
	}

	return stages;
}

vk::PipelineStageFlags2 ImageUsageToStages2(vk::ImageUsageFlags usage) {}

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
	if (defaultView) {
		const ImageViewCreateInfo viewCI{.Image       = this,
		                                 .Format      = _createInfo.Format,
		                                 .BaseLevel   = 0,
		                                 .MipLevels   = _createInfo.MipLevels,
		                                 .BaseLayer   = 0,
		                                 .ArrayLayers = _createInfo.ArrayLayers,
		                                 .ViewType    = viewType};
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

	if (_memoryOwned) {
		if (_internalSync) {
			_device.FreeAllocationNoLock(_allocation);
		} else {
			_device.FreeAllocation(_allocation);
		}
	}
}

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
		if (_unormView) { _device.DestroyImageViewNoLock(_unormView); }
		if (_srgbView) { _device.DestroyImageViewNoLock(_srgbView); }
		for (auto& view : _renderTargetViews) { _device.DestroyImageViewNoLock(view); }
	} else {
		_device.DestroyImageView(_view);
		if (_depthView) { _device.DestroyImageView(_depthView); }
		if (_stencilView) { _device.DestroyImageView(_stencilView); }
		if (_unormView) { _device.DestroyImageView(_unormView); }
		if (_srgbView) { _device.DestroyImageView(_srgbView); }
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

uint32_t ImageView::GetWidth() const {
	return _createInfo.Image->GetWidth();
}

uint32_t ImageView::GetHeight() const {
	return _createInfo.Image->GetHeight();
}

uint32_t ImageView::GetDepth() const {
	return _createInfo.Image->GetDepth();
}
}  // namespace Vulkan
}  // namespace Luna
