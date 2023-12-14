#include <Luna/Vulkan/Buffer.hpp>
#include <Luna/Vulkan/CommandBuffer.hpp>
#include <Luna/Vulkan/Device.hpp>
#include <Luna/Vulkan/Image.hpp>
#include <Luna/Vulkan/Semaphore.hpp>
#include <Luna/Vulkan/TextureFormat.hpp>

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
		// Ensure image has a valid size.
		if (createInfo.Width == 0 || createInfo.Height == 0 || createInfo.Depth == 0) {
			throw std::logic_error("Cannot create an image with 0 in any dimension");
		}

		// Ensure image has a valid format.
		if (createInfo.Format == vk::Format::eUndefined) {
			throw std::logic_error("Cannot create an image in Undefined format");
		}

		// Ensure image has a valid array layer count.
		if (createInfo.ArrayLayers == 0) { throw std::logic_error("Cannot create an image with 0 array layers"); }

		// Ensure the device supports the image format for this usage.
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

		// Ensure the image does not exceed size restrictions.
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

		// Ensure the image does not exceed layer count restrictions.
		if (createInfo.ArrayLayers > properties.maxArrayLayers) {
			Log::Error(
				"Vulkan", "Image has too many layers: {} {} ({})", createInfo.Type, createInfo.Format, createInfo.ArrayLayers);
			Log::Error("Vulkan", "  Maximum allowable layers is: {}", properties.maxArrayLayers);
			throw std::runtime_error("Device does not support images with this many layers");
		}

		// Ensure the image does not exceed mip level count restrictions.
		if (createInfo.MipLevels > properties.maxMipLevels) {
			Log::Error("Vulkan",
			           "Image has too many mip levels: {} {} ({})",
			           createInfo.Type,
			           createInfo.Format,
			           createInfo.MipLevels);
			Log::Error("Vulkan", "  Maximum allowable mip levels is: {}", properties.maxMipLevels);
			throw std::runtime_error("Device does not support images with this many mip levels");
		}

		// Ensure the device supports the chosen sample count.
		if ((createInfo.Samples & properties.sampleCounts) != createInfo.Samples) {
			Log::Error(
				"Vulkan", "Unsupported sample count: {} {} ({})", createInfo.Type, createInfo.Format, createInfo.Samples);
			Log::Error("Vulkan", "  Allowable sample counts are: {}", properties.sampleCounts);
			throw std::runtime_error("Device does not support images with this sample count");
		}
	}

	// Allow the implementation to use lazy memory allocation for transient attachments.
	if (_createInfo.Domain == ImageDomain::Transient) {
		_createInfo.Usage |= vk::ImageUsageFlagBits::eTransientAttachment;
	}

	// If we have been given an initial image, we need to be able to transfer into this image.
	if (initialData) { _createInfo.Usage |= vk::ImageUsageFlagBits::eTransferDst; }

	// If we have to generate mips, we need to be able to transfer from this image.
	const bool generateMips = _createInfo.MiscFlags & ImageCreateFlagBits::GenerateMipmaps;
	if (generateMips) { _createInfo.Usage |= vk::ImageUsageFlagBits::eTransferSrc; }

	// Automatically calculate mip levels when they're not provided.
	if (_createInfo.MipLevels == 0) {
		_createInfo.MipLevels = CalculateMipLevels(_createInfo.Width, _createInfo.Height, _createInfo.Depth);
	}

	// Create sRGB and UNorm views for storage images, as compute shaders cannot write to sRGB images.
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

	// Determine which queues will be sharing this image, if any.
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

	// Create the image.
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

	// Set the image's debug name, if applicable.
	if (_debugName.empty()) {
		Log::Trace("Vulkan", "Image created. ({} {})", createInfo.Format, imageCI.extent);
	} else {
		_device.SetObjectName(_image, _debugName);
		vmaSetAllocationName(_device._allocator, _allocation, _debugName.c_str());
		Log::Trace("Vulkan", "Image \"{}\" created. ({} {})", _debugName, createInfo.Format, imageCI.extent);
	}

	// Create image's default views.
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

		// Default View. Includes all mip levels and array layers.
		{
			defaultView = _device._device.createImageView(viewCI);
			if (!_debugName.empty()) { _device.SetObjectName(defaultView, std::format("{} View", _debugName)); }
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
					_device.SetObjectName(_imageView->_depthView, std::format("{} Depth View", _debugName));
				}
				Log::Trace("Vulkan", "  Image View created. (Depth)");

				viewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eStencil;
				_imageView->_stencilView             = _device._device.createImageView(viewInfo);
				if (!_debugName.empty()) {
					_device.SetObjectName(_imageView->_stencilView, std::format("{} Stencil View", _debugName));
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
							std::format("{} Render Target View Layer {}", _debugName, viewInfo.subresourceRange.baseArrayLayer));
					}
					Log::Trace(
						"Vulkan", "  Image View created. (Render Target Layer {})", viewInfo.subresourceRange.baseArrayLayer);
					_imageView->_renderTargetViews.push_back(view);
				}
			}
		}
	}

	// Handle initial upload of image data, if applicable.
	BufferHandle stagingBuffer;
	std::vector<vk::BufferImageCopy> blits;
	if (initialData) {
		// Create our staging buffer.
		{
			TextureFormatLayout layout;

			uint32_t copyLevels = _createInfo.MipLevels;
			if (generateMips) {
				copyLevels = 1;
			} else if (_createInfo.MipLevels == 0) {
				copyLevels = CalculateMipLevels(_createInfo.Width, _createInfo.Height, _createInfo.Depth);
			}

			switch (_createInfo.Type) {
				case vk::ImageType::e1D:
					layout.Set1D(_createInfo.Format, _createInfo.Width, _createInfo.ArrayLayers, copyLevels);
					break;
				case vk::ImageType::e2D:
					layout.Set2D(_createInfo.Format, _createInfo.Width, _createInfo.Height, _createInfo.ArrayLayers, copyLevels);
					break;
				case vk::ImageType::e3D:
					layout.Set3D(_createInfo.Format, _createInfo.Width, _createInfo.Height, _createInfo.Depth, copyLevels);
					break;
				default:
					throw std::logic_error("Cannot upload initial image data");
			}

			const BufferCreateInfo bufferCI{
				BufferDomain::Host, layout.GetRequiredSize(), vk::BufferUsageFlagBits::eTransferSrc};
			stagingBuffer = _device.CreateBuffer(bufferCI);

			uint32_t index = 0;
			uint8_t* data  = reinterpret_cast<uint8_t*>(stagingBuffer->Map());
			layout.SetBuffer(layout.GetRequiredSize(), data);

			for (uint32_t level = 0; level < copyLevels; ++level) {
				const auto& mipInfo            = layout.GetMipInfo(level);
				const uint32_t dstHeightStride = layout.GetLayerSize(level);
				const size_t rowSize           = layout.GetRowSize(level);

				for (uint32_t layer = 0; layer < _createInfo.ArrayLayers; ++layer, ++index) {
					const uint32_t srcRowLength = initialData[index].RowLength ? initialData[index].RowLength : mipInfo.RowLength;
					const uint32_t srcArrayHeight =
						initialData[index].ImageHeight ? initialData[index].ImageHeight : mipInfo.ImageHeight;
					const uint32_t srcRowStride    = layout.RowByteStride(srcRowLength);
					const uint32_t srcHeightStride = layout.LayerByteStride(srcArrayHeight, srcRowStride);

					uint8_t* dst       = reinterpret_cast<uint8_t*>(layout.Data(layer, level));
					const uint8_t* src = reinterpret_cast<const uint8_t*>(initialData[index].Data);
					for (uint32_t z = 0; z < mipInfo.Depth; ++z) {
						for (uint32_t y = 0; y < mipInfo.BlockImageHeight; ++y) {
							memcpy(dst + z * dstHeightStride + y * rowSize, src + z * srcHeightStride + y * srcRowStride, rowSize);
						}
					}
				}
			}

			blits = layout.BuildBufferImageCopies();
		}
	}

	CommandBufferHandle transitionCmd;
	if (stagingBuffer) {
		const bool generateMips = _createInfo.MiscFlags & ImageCreateFlagBits::GenerateMipmaps;

		{
			auto transferCmd = _device.RequestCommandBuffer(CommandBufferType::AsyncTransfer);

			{
				transferCmd->ImageBarrier(*this,
				                          vk::ImageLayout::eUndefined,
				                          vk::ImageLayout::eTransferDstOptimal,
				                          vk::PipelineStageFlagBits2::eNone,
				                          vk::AccessFlagBits2::eNone,
				                          vk::PipelineStageFlagBits2::eCopy,
				                          vk::AccessFlagBits2::eTransferWrite);
				transferCmd->CopyBufferToImage(*this, *stagingBuffer, blits);
				if (!generateMips) {
					transferCmd->ImageBarrier(*this,
					                          vk::ImageLayout::eTransferDstOptimal,
					                          _createInfo.InitialLayout,
					                          vk::PipelineStageFlagBits2::eCopy,
					                          vk::AccessFlagBits2::eTransferWrite,
					                          vk::PipelineStageFlagBits2::eNone,
					                          vk::AccessFlagBits2::eNone);
					transitionCmd = std::move(transferCmd);
				}
			}

			if (generateMips) {
				std::vector<SemaphoreHandle> semaphores(1);
				_device.Submit(transferCmd, nullptr, &semaphores);
				_device.AddWaitSemaphore(CommandBufferType::Generic, semaphores[0], vk::PipelineStageFlagBits2::eBlit, true);
			}
		}

		if (generateMips) {
			auto graphicsCmd = _device.RequestCommandBuffer(CommandBufferType::Generic);
			graphicsCmd->BarrierPrepareGenerateMipmaps(*this,
			                                           vk::ImageLayout::eTransferDstOptimal,
			                                           vk::PipelineStageFlagBits2::eBlit,
			                                           vk::AccessFlagBits2::eNone,
			                                           true);
			graphicsCmd->GenerateMipmaps(*this);

			graphicsCmd->ImageBarrier(*this,
			                          vk::ImageLayout::eTransferSrcOptimal,
			                          _createInfo.InitialLayout,
			                          vk::PipelineStageFlagBits2::eBlit,
			                          vk::AccessFlagBits2::eNone,
			                          vk::PipelineStageFlagBits2::eNone,
			                          vk::AccessFlagBits2::eNone);

			transitionCmd = std::move(graphicsCmd);
		}
	} else if (_createInfo.InitialLayout != vk::ImageLayout::eUndefined) {
		CommandBufferType type = CommandBufferType::Generic;

		if (queueFlags & ImageCreateFlagBits::ConcurrentQueueGraphics) {
			type = CommandBufferType::Generic;
		} else if (queueFlags & ImageCreateFlagBits::ConcurrentQueueAsyncGraphics) {
			type = CommandBufferType::AsyncGraphics;
		} else if (queueFlags & ImageCreateFlagBits::ConcurrentQueueAsyncCompute) {
			type = CommandBufferType::AsyncCompute;
		} else if (queueFlags & ImageCreateFlagBits::ConcurrentQueueAsyncTransfer) {
			type = CommandBufferType::AsyncTransfer;
		}

		transitionCmd = _device.RequestCommandBuffer(type);
		transitionCmd->ImageBarrier(*this,
		                            imageCI.initialLayout,
		                            _createInfo.InitialLayout,
		                            vk::PipelineStageFlagBits2::eNone,
		                            vk::AccessFlagBits2::eNone,
		                            vk::PipelineStageFlagBits2::eNone,
		                            vk::AccessFlagBits2::eNone);
	}

	if (transitionCmd) {
		std::array<vk::PipelineStageFlags2, QueueTypeCount> stages;
		std::array<CommandBufferType, QueueTypeCount> types;
		std::array<SemaphoreHandle, QueueTypeCount> semaphores;
		uint32_t semaphoreCount = 0;

		if (queueFlags & ImageCreateFlagBits::ConcurrentQueueAsyncGraphics) {
			if (_device.GetQueueType(CommandBufferType::AsyncGraphics) == QueueType::Graphics) {
				queueFlags |= ImageCreateFlagBits::ConcurrentQueueGraphics;
				queueFlags &= ~ImageCreateFlagBits::ConcurrentQueueAsyncGraphics;
			} else {
				queueFlags &= ~ImageCreateFlagBits::ConcurrentQueueAsyncCompute;
			}
		}

		if (queueFlags & ImageCreateFlagBits::ConcurrentQueueGraphics) {
			types[semaphoreCount]  = CommandBufferType::Generic;
			stages[semaphoreCount] = vk::PipelineStageFlagBits2::eAllCommands;
			semaphoreCount++;
		} else if (queueFlags & ImageCreateFlagBits::ConcurrentQueueAsyncGraphics) {
			types[semaphoreCount]  = CommandBufferType::AsyncGraphics;
			stages[semaphoreCount] = vk::PipelineStageFlagBits2::eAllCommands;
			semaphoreCount++;
		}
		if (queueFlags & ImageCreateFlagBits::ConcurrentQueueAsyncCompute) {
			types[semaphoreCount]  = CommandBufferType::AsyncCompute;
			stages[semaphoreCount] = vk::PipelineStageFlagBits2::eAllCommands;
			semaphoreCount++;
		}
		if (queueFlags & ImageCreateFlagBits::ConcurrentQueueAsyncTransfer) {
			types[semaphoreCount]  = CommandBufferType::AsyncTransfer;
			stages[semaphoreCount] = vk::PipelineStageFlagBits2::eAllCommands;
			semaphoreCount++;
		}

		std::vector<SemaphoreHandle> sem(semaphores.data(), semaphores.data() + semaphoreCount);
		_device.Submit(transitionCmd, nullptr, &sem);
		for (uint32_t i = 0; i < semaphoreCount; ++i) { _device.AddWaitSemaphore(types[i], sem[i], stages[i], true); }
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

void Image::SetLayoutType(ImageLayoutType type) noexcept {
	_layoutType = type;
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

	// Unreachable code, but MSVC complains.
	return vk::ImageViewType::e1D;
}

void ImageViewDeleter::operator()(ImageView* imageView) {
	imageView->_device._imageViewPool.Free(imageView);
}

ImageView::ImageView(Device& device, const ImageViewCreateInfo& createInfo)
		: Cookie(device), _device(device), _createInfo(createInfo) {
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
		: Cookie(device), _device(device), _createInfo(createInfo), _view(view) {}

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

vk::ImageView ImageView::GetRenderTargetView(uint32_t layer) const noexcept {
	if (_createInfo.Image->GetCreateInfo().Domain == ImageDomain::Transient) { return _view; }
	if (_renderTargetViews.empty()) { return _view; }
	return _renderTargetViews[layer];
}
}  // namespace Vulkan
}  // namespace Luna
