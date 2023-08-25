#include <Luna/Vulkan/Buffer.hpp>
#include <Luna/Vulkan/Device.hpp>
#include <Luna/Vulkan/Image.hpp>

namespace Luna {
namespace Vulkan {
void ImageDeleter::operator()(Image* image) {
	image->_device._imagePool.Free(image);
}

Image::Image(Device& device,
             const ImageCreateInfo& createInfo,
             const ImageInitialData* initialData,
             const std::string debugName)
		: Cookie(device), _device(device), _createInfo(createInfo), _debugName(debugName) {
	// Sanity checks.
	{
		if (createInfo.Width == 0 || createInfo.Height == 0 || createInfo.Depth == 0) {
			throw std::logic_error("Cannot create an image with 0 in any dimension");
		}
		if (createInfo.Format == vk::Format::eUndefined) {
			throw std::logic_error("Cannot create an image in Undefined format");
		}
		if (createInfo.ArrayLayers == 0) { throw std::logic_error("Cannot create an image with 0 array layers"); }

		vk::ImageFormatProperties properties = {};
		try {
			properties = _device._deviceInfo.PhysicalDevice.getImageFormatProperties(
				createInfo.Format, createInfo.Type, vk::ImageTiling::eOptimal, createInfo.Usage, createInfo.Flags);
		} catch (const vk::FormatNotSupportedError& e) {
			Log::Error("Vulkan",
			           "Unsupported Image: {} {}, Usage {}, Flags {}",
			           createInfo.Type,
			           createInfo.Format,
			           createInfo.Usage,
			           createInfo.Flags);
			throw std::runtime_error("Device does not support creating an image with these options");
		}

		if (createInfo.Width > properties.maxExtent.width || createInfo.Height > properties.maxExtent.height ||
		    createInfo.Depth > properties.maxExtent.depth) {
			Log::Error("Vulkan",
			           "Image too large: {} {} ({})",
			           createInfo.Type,
			           createInfo.Format,
			           vk::Extent3D(createInfo.Width, createInfo.Height, createInfo.Depth));
			Log::Error("Vulkan", "  Maximum allowable size is: {}", properties.maxExtent);
			throw std::runtime_error("Device does not support images this large");
		}

		if (createInfo.ArrayLayers > properties.maxArrayLayers) {
			Log::Error(
				"Vulkan", "Image has too many layers: {} {} ({})", createInfo.Type, createInfo.Format, createInfo.ArrayLayers);
			Log::Error("Vulkan", "  Maximum allowable layers is: {}", properties.maxArrayLayers);
			throw std::runtime_error("Device does not support images with this many layers");
		}

		if (createInfo.MipLevels > properties.maxMipLevels) {
			Log::Error("Vulkan",
			           "Image has too many mip levels: {} {} ({})",
			           createInfo.Type,
			           createInfo.Format,
			           createInfo.MipLevels);
			Log::Error("Vulkan", "  Maximum allowable mip levels is: {}", properties.maxMipLevels);
			throw std::runtime_error("Device does not support images with this many mip levels");
		}

		if ((createInfo.Samples & properties.sampleCounts) != createInfo.Samples) {
			Log::Error(
				"Vulkan", "Unsupported sample count: {} {} ({})", createInfo.Type, createInfo.Format, createInfo.Samples);
			Log::Error("Vulkan", "  Allowable sample counts are: {}", properties.sampleCounts);
			throw std::runtime_error("Device does not support images with this sample count");
		}
	}

	const bool generateMips = _createInfo.MiscFlags & ImageCreateFlagBits::GenerateMipmaps;
	if (_createInfo.Domain == ImageDomain::Transient) {
		_createInfo.Usage |= vk::ImageUsageFlagBits::eTransientAttachment;
	}
	if (initialData) { _createInfo.Usage |= vk::ImageUsageFlagBits::eTransferDst; }
	if (generateMips) { _createInfo.Usage |= vk::ImageUsageFlagBits::eTransferSrc; }
	if (_createInfo.MipLevels == 0) {
		_createInfo.MipLevels = CalculateMipLevels(_createInfo.Width, _createInfo.Height, _createInfo.Depth);
	}
	if ((_createInfo.Usage & vk::ImageUsageFlagBits::eStorage) &&
	    (_createInfo.MiscFlags & ImageCreateFlagBits::MutableSrgb)) {
		_createInfo.Flags |= vk::ImageCreateFlagBits::eMutableFormat;
	}

	vk::ImageCreateInfo imageCI(_createInfo.Flags,
	                            _createInfo.Type,
	                            _createInfo.Format,
	                            vk::Extent3D(_createInfo.Width, _createInfo.Height, _createInfo.Depth),
	                            _createInfo.MipLevels,
	                            _createInfo.ArrayLayers,
	                            _createInfo.Samples,
	                            vk::ImageTiling::eOptimal,
	                            _createInfo.Usage,
	                            vk::SharingMode::eExclusive,
	                            nullptr,
	                            vk::ImageLayout::eUndefined);

	ImageCreateFlags queueFlags =
		_createInfo.MiscFlags &
		(ImageCreateFlagBits::ConcurrentQueueGraphics | ImageCreateFlagBits::ConcurrentQueueAsyncCompute |
	   ImageCreateFlagBits::ConcurrentQueueAsyncGraphics | ImageCreateFlagBits::ConcurrentQueueAsyncTransfer);
	std::vector<uint32_t> sharingIndices;
	{
		std::set<uint32_t> uniqueIndices;
		const bool concurrentQueue =
			bool(queueFlags) || initialData || _createInfo.InitialLayout != vk::ImageLayout::eUndefined;
		if (concurrentQueue) {
			if (initialData && !bool(queueFlags)) {
				queueFlags |= ImageCreateFlagBits::ConcurrentQueueGraphics;
				queueFlags |= ImageCreateFlagBits::ConcurrentQueueAsyncGraphics;
				queueFlags |= ImageCreateFlagBits::ConcurrentQueueAsyncCompute;
				queueFlags |= ImageCreateFlagBits::ConcurrentQueueAsyncTransfer;
			} else if (initialData) {
				queueFlags |= ImageCreateFlagBits::ConcurrentQueueAsyncTransfer;
				if (generateMips) { queueFlags |= ImageCreateFlagBits::ConcurrentQueueGraphics; }
			}

			struct Mapping {
				ImageCreateFlags Flags;
				QueueType Queue;
			};
			constexpr Mapping Mappings[] = {
				{ImageCreateFlagBits::ConcurrentQueueGraphics | ImageCreateFlagBits::ConcurrentQueueAsyncGraphics,
			   QueueType::Graphics},
				{ImageCreateFlagBits::ConcurrentQueueAsyncCompute, QueueType::Compute},
				{ImageCreateFlagBits::ConcurrentQueueAsyncTransfer, QueueType::Transfer}};

			for (auto& map : Mappings) {
				if (queueFlags & map.Flags) { uniqueIndices.insert(_device._queueInfo.Family(map.Queue)); }
			}

			sharingIndices = std::vector<uint32_t>(uniqueIndices.begin(), uniqueIndices.end());
			if (sharingIndices.size() > 1) {
				imageCI.sharingMode = vk::SharingMode::eConcurrent;
				imageCI.setQueueFamilyIndices(sharingIndices);
			} else {
				imageCI.sharingMode = vk::SharingMode::eExclusive;
				imageCI.setQueueFamilyIndices(nullptr);
			}
		}
		if (!queueFlags) { queueFlags |= ImageCreateFlagBits::ConcurrentQueueGraphics; }
	}

	{
		std::lock_guard<std::mutex> lock(_device._lock.MemoryLock);

		VkImageCreateInfo cImageCI = imageCI;
		const VmaAllocationCreateInfo imageAI{.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE};
		VkImage image = VK_NULL_HANDLE;
		const VkResult createResult =
			vmaCreateImage(_device._allocator, &cImageCI, &imageAI, &image, &_allocation, nullptr);
		if (createResult != VK_SUCCESS) {
			Log::Error("Vulkan", "Failed to create image: {}", vk::to_string(vk::Result(createResult)));

			throw std::runtime_error("Failed to create buffer");
		}
		_image = image;
	}

	if (_debugName.empty()) {
		Log::Trace("Vulkan", "Image created. ({} {})", createInfo.Format, imageCI.extent);
	} else {
		_device.SetObjectName(_image, _debugName);
		vmaSetAllocationName(_device._allocator, _allocation, _debugName.c_str());
		Log::Trace("Vulkan", "Image \"{}\" created. ({} {})", _debugName, createInfo.Format, imageCI.extent);
	}

	const bool hasView(createInfo.Usage &
	                   (vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage |
	                    vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eDepthStencilAttachment |
	                    vk::ImageUsageFlagBits::eInputAttachment));
	if (hasView) {
		vk::ImageView defaultView;

		vk::ImageViewCreateInfo viewCI(
			{},
			_image,
			GetImageViewType(_createInfo, nullptr),
			_createInfo.Format,
			vk::ComponentMapping(),
			vk::ImageSubresourceRange(
				FormatAspectFlags(_createInfo.Format), 0, _createInfo.MipLevels, 0, _createInfo.ArrayLayers));

		// Default View
		{
			defaultView = _device._device.createImageView(viewCI);
			if (!_debugName.empty()) { _device.SetObjectName(defaultView, fmt::format("{} View", _debugName)); }
			Log::Trace("Vulkan", "  Image View created.");
		}

		{
			const ImageViewCreateInfo viewInfo{.Image       = this,
			                                   .Format      = _createInfo.Format,
			                                   .BaseLevel   = 0,
			                                   .MipLevels   = _createInfo.MipLevels,
			                                   .BaseLayer   = 0,
			                                   .ArrayLayers = _createInfo.ArrayLayers,
			                                   .ViewType    = viewCI.viewType,
			                                   .Swizzle     = vk::ComponentMapping(),
			                                   .MiscFlags   = {}};
			_imageView = ImageViewHandle(_device._imageViewPool.Allocate(_device, defaultView, viewInfo));
		}

		// Depth/Stencil Views
		if (viewCI.subresourceRange.aspectMask == (vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil)) {
			if ((_createInfo.Usage & ~vk::ImageUsageFlagBits::eDepthStencilAttachment) &&
			    viewCI.viewType != vk::ImageViewType::eCube && viewCI.viewType != vk::ImageViewType::eCubeArray &&
			    viewCI.viewType != vk::ImageViewType::e3D) {
				auto viewInfo = viewCI;

				viewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth;
				_imageView->_depthView               = _device._device.createImageView(viewInfo);
				if (!_debugName.empty()) {
					_device.SetObjectName(_imageView->_depthView, fmt::format("{} Depth View", _debugName));
				}
				Log::Trace("Vulkan", "  Image View created. (Depth)");

				viewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eStencil;
				_imageView->_stencilView             = _device._device.createImageView(viewInfo);
				if (!_debugName.empty()) {
					_device.SetObjectName(_imageView->_stencilView, fmt::format("{} Stencil View", _debugName));
				}
				Log::Trace("Vulkan", "  Image View created. (Stencil)");
			}
		}

		// Render Target Views
		if ((_createInfo.Usage &
		     (vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eDepthStencilAttachment)) &&
		    ((viewCI.subresourceRange.levelCount > 1) || (viewCI.subresourceRange.layerCount > 1))) {
			if (viewCI.viewType != vk::ImageViewType::e3D) {
				_imageView->_renderTargetViews.reserve(viewCI.subresourceRange.layerCount);

				auto viewInfo = viewCI;

				viewInfo.viewType                    = vk::ImageViewType::e2D;
				viewInfo.subresourceRange.levelCount = 1;
				viewInfo.subresourceRange.layerCount = 1;

				for (uint32_t layer = 0; layer < viewCI.subresourceRange.layerCount; ++layer) {
					viewInfo.subresourceRange.baseArrayLayer = layer + viewCI.subresourceRange.baseArrayLayer;
					vk::ImageView view                       = _device._device.createImageView(viewInfo);
					if (!_debugName.empty()) {
						_device.SetObjectName(
							view,
							fmt::format("{} Render Target View Layer {}", _debugName, viewInfo.subresourceRange.baseArrayLayer));
					}
					Log::Trace(
						"Vulkan", "  Image View created. (Render Target Layer {})", viewInfo.subresourceRange.baseArrayLayer);
					_imageView->_renderTargetViews.push_back(view);
				}
			}
		}
	}
}

Image::Image(Device& device,
             const ImageCreateInfo& createInfo,
             vk::Image image,
             VmaAllocation allocation,
             vk::ImageView imageView,
             const std::string& debugName)
		: Cookie(device), _device(device), _createInfo(createInfo), _image(image), _allocation(allocation) {
	const ImageViewCreateInfo viewInfo{.Image       = this,
	                                   .Format      = _createInfo.Format,
	                                   .BaseLevel   = 0,
	                                   .MipLevels   = _createInfo.MipLevels,
	                                   .BaseLayer   = 0,
	                                   .ArrayLayers = _createInfo.ArrayLayers,
	                                   .ViewType    = GetImageViewType(_createInfo, nullptr),
	                                   .Swizzle     = vk::ComponentMapping(),
	                                   .MiscFlags   = {}};
	_imageView = ImageViewHandle(_device._imageViewPool.Allocate(_device, imageView, viewInfo));
}

Image::~Image() noexcept {
	if (_internalSync) {
		if (_image && _imageOwned) { _device.DestroyImageNoLock(_image); }
		if (_allocation && _allocationOwned) { _device.FreeAllocationNoLock(_allocation, false); }
	} else {
		if (_image && _imageOwned) { _device.DestroyImage(_image); }
		if (_allocation && _allocationOwned) { _device.FreeAllocation(_allocation, false); }
	}
}

void Image::DisownImage() noexcept {
	_imageOwned = false;
}

void Image::DisownMemory() noexcept {
	_allocationOwned = false;
}

void Image::SetSwapchainLayout(vk::ImageLayout layout) noexcept {
	_swapchainLayout = layout;
}

constexpr vk::ImageViewType Image::GetImageViewType(const ImageCreateInfo& imageCI, const ImageViewCreateInfo* viewCI) {
	const uint32_t baseLayer = viewCI ? viewCI->BaseLayer : 0;
	uint32_t layers          = viewCI ? viewCI->ArrayLayers : imageCI.ArrayLayers;
	if (layers == VK_REMAINING_ARRAY_LAYERS) { layers = imageCI.ArrayLayers - baseLayer; }

	const bool forceArray = viewCI ? bool(viewCI->MiscFlags & ImageViewCreateFlagBits::ForceArray)
	                               : bool(imageCI.MiscFlags & ImageCreateFlagBits::ForceArray);

	switch (imageCI.Type) {
		case vk::ImageType::e1D:
			if (layers > 1 || forceArray) {
				return vk::ImageViewType::e1DArray;
			} else {
				return vk::ImageViewType::e1D;
			}
			break;

		case vk::ImageType::e2D:
			if ((imageCI.Flags & vk::ImageCreateFlagBits::eCubeCompatible) && (layers % 6) == 0) {
				if (layers > 6 || forceArray) {
					return vk::ImageViewType::eCubeArray;
				} else {
					return vk::ImageViewType::eCube;
				}
			} else {
				if (layers > 1 || forceArray) {
					return vk::ImageViewType::e2DArray;
				} else {
					return vk::ImageViewType::e2D;
				}
			}
			break;

		case vk::ImageType::e3D:
			return vk::ImageViewType::e3D;
	}
}

void ImageViewDeleter::operator()(ImageView* imageView) {
	imageView->_device._imageViewPool.Free(imageView);
}

ImageView::ImageView(Device& device, const ImageViewCreateInfo& createInfo) : _device(device), _createInfo(createInfo) {
	const vk::ImageViewCreateInfo viewCI({},
	                                     _createInfo.Image->GetImage(),
	                                     _createInfo.ViewType,
	                                     _createInfo.Format,
	                                     _createInfo.Swizzle,
	                                     vk::ImageSubresourceRange(FormatAspectFlags(_createInfo.Format),
	                                                               _createInfo.BaseLevel,
	                                                               _createInfo.MipLevels,
	                                                               _createInfo.BaseLayer,
	                                                               _createInfo.ArrayLayers));
	_view = _device._device.createImageView(viewCI);
	Log::Trace("Vulkan", "Image View created.");
}

ImageView::ImageView(Device& device, vk::ImageView view, const ImageViewCreateInfo& createInfo)
		: _device(device), _createInfo(createInfo), _view(view) {}

ImageView::~ImageView() noexcept {
	if (_internalSync) {
		if (_view) { _device.DestroyImageViewNoLock(_view); }
		if (_depthView) { _device.DestroyImageViewNoLock(_depthView); }
		if (_stencilView) { _device.DestroyImageViewNoLock(_stencilView); }
		if (_unormView) { _device.DestroyImageViewNoLock(_unormView); }
		if (_srgbView) { _device.DestroyImageViewNoLock(_srgbView); }
		for (auto view : _renderTargetViews) { _device.DestroyImageViewNoLock(view); }
	} else {
		if (_view) { _device.DestroyImageView(_view); }
		if (_depthView) { _device.DestroyImageView(_depthView); }
		if (_stencilView) { _device.DestroyImageView(_stencilView); }
		if (_unormView) { _device.DestroyImageView(_unormView); }
		if (_srgbView) { _device.DestroyImageView(_srgbView); }
		for (auto view : _renderTargetViews) { _device.DestroyImageView(view); }
	}
}
}  // namespace Vulkan
}  // namespace Luna
