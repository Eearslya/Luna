#include "Device.hpp"

#include "Buffer.hpp"
#include "CommandBuffer.hpp"
#include "CommandPool.hpp"
#include "Context.hpp"
#include "Fence.hpp"
#include "Format.hpp"
#include "Image.hpp"
#include "Semaphore.hpp"
#include "TextureFormat.hpp"
#include "Utility/Log.hpp"

#ifdef LUNA_VULKAN_MT
static uint32_t GetThreadIndex() {
	return 0;
}
#	define DeviceLock() std::lock_guard<std::mutex> lock(_lock.Mutex)
#	define DeviceFlush()                                                 \
		do {                                                                \
			std::unique_lock<std::mutex> lock(_lock.Mutex);                   \
			_lock.Condition.wait(lock, [&]() { return _lock.Counter == 0; }); \
		} while (0)
#else
static uint32_t GetThreadIndex() {
	return 0;
}
#	define DeviceLock()  ((void) 0)
#	define DeviceFlush() assert(_lock.Counter == 0)
#endif

namespace Luna {
namespace Vulkan {
constexpr static QueueType QueueFlushOrder[] = {QueueType::Transfer, QueueType::Graphics, QueueType::Compute};

class ImageResourceHolder {
 public:
	explicit ImageResourceHolder(Device& device) : _device(device) {}
	~ImageResourceHolder() noexcept {}

	vk::Image Image;
	VmaAllocation Allocation;

 private:
	Device& _device;
};

Device::Device(const Context& context)
		: _extensions(context.GetExtensionInfo()),
			_instance(context.GetInstance()),
			_gpuInfo(context.GetGPUInfo()),
			_queues(context.GetQueueInfo()),
			_gpu(context.GetGPU()),
			_device(context.GetDevice()) {
#ifdef LUNA_VULKAN_MT
	_cookie.store(0);
#endif

	// Initialize our VMA allocator.
	{
#define FN(name) .name = VULKAN_HPP_DEFAULT_DISPATCHER.name
		VmaVulkanFunctions vmaFunctions = {FN(vkGetInstanceProcAddr),
		                                   FN(vkGetDeviceProcAddr),
		                                   FN(vkGetPhysicalDeviceProperties),
		                                   FN(vkGetPhysicalDeviceMemoryProperties),
		                                   FN(vkAllocateMemory),
		                                   FN(vkFreeMemory),
		                                   FN(vkMapMemory),
		                                   FN(vkUnmapMemory),
		                                   FN(vkFlushMappedMemoryRanges),
		                                   FN(vkInvalidateMappedMemoryRanges),
		                                   FN(vkBindBufferMemory),
		                                   FN(vkBindImageMemory),
		                                   FN(vkGetBufferMemoryRequirements),
		                                   FN(vkGetImageMemoryRequirements),
		                                   FN(vkCreateBuffer),
		                                   FN(vkDestroyBuffer),
		                                   FN(vkCreateImage),
		                                   FN(vkDestroyImage),
		                                   FN(vkCmdCopyBuffer)};
#undef FN
#define FN(core) vmaFunctions.core##KHR = reinterpret_cast<PFN_##core##KHR>(VULKAN_HPP_DEFAULT_DISPATCHER.core)
		FN(vkGetBufferMemoryRequirements2);
		FN(vkGetImageMemoryRequirements2);
		FN(vkBindBufferMemory2);
		FN(vkBindImageMemory2);
		FN(vkGetPhysicalDeviceMemoryProperties2);
#undef FN
		if (_extensions.Maintenance4) {
			vmaFunctions.vkGetDeviceBufferMemoryRequirements =
				VULKAN_HPP_DEFAULT_DISPATCHER.vkGetDeviceBufferMemoryRequirementsKHR;
			vmaFunctions.vkGetDeviceImageMemoryRequirements =
				VULKAN_HPP_DEFAULT_DISPATCHER.vkGetDeviceImageMemoryRequirementsKHR;
		}

		const VmaAllocatorCreateInfo allocatorCI = {.physicalDevice   = _gpu,
		                                            .device           = _device,
		                                            .pVulkanFunctions = &vmaFunctions,
		                                            .instance         = _instance,
		                                            .vulkanApiVersion = VK_API_VERSION_1_1};
		const auto allocatorResult               = vmaCreateAllocator(&allocatorCI, &_allocator);
		if (allocatorResult != VK_SUCCESS) {
			throw std::runtime_error("[Vulkan::Device] Failed to create memory allocator!");
		}
	}

	CreateTimelineSemaphores();

	// Create our frame contexts.
	{
		DeviceFlush();
		WaitIdleNoLock();

		_frameContexts.clear();
		for (int i = 0; i < 2; ++i) {
			auto frame = std::make_unique<FrameContext>(*this, i);
			_frameContexts.emplace_back(std::move(frame));
		}
	}
}

Device::~Device() noexcept {
	WaitIdle();

	vmaDestroyAllocator(_allocator);

	for (auto& semaphore : _availableSemaphores) { _device.destroySemaphore(semaphore); }
	for (auto& fence : _availableFences) { _device.destroyFence(fence); }

	DestroyTimelineSemaphores();
}

BufferHandle Device::CreateBuffer(const BufferCreateInfo& createInfo, const void* initialData) {
	BufferCreateInfo actualCI = createInfo;
	actualCI.Usage |= vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eTransferSrc;

	auto queueFamilies = _queues.UniqueFamilies();

	VmaAllocationCreateFlags allocFlags = {};
	if (actualCI.Domain == BufferDomain::Host) {
		allocFlags |= VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
	}

	const vk::BufferCreateInfo bufferCI(
		{},
		actualCI.Size,
		actualCI.Usage,
		queueFamilies.size() == 1 ? vk::SharingMode::eExclusive : vk::SharingMode::eConcurrent,
		queueFamilies);
	const VmaAllocationCreateInfo bufferAI = {.flags         = allocFlags,
	                                          .usage         = VMA_MEMORY_USAGE_AUTO,
	                                          .requiredFlags = actualCI.Domain == BufferDomain::Host
	                                                             ? VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
	                                                             : VkMemoryPropertyFlags{}};

	VkBuffer buffer;
	VmaAllocation allocation;
	VmaAllocationInfo allocationInfo;
	const auto result = vmaCreateBuffer(_allocator,
	                                    reinterpret_cast<const VkBufferCreateInfo*>(&bufferCI),
	                                    &bufferAI,
	                                    &buffer,
	                                    &allocation,
	                                    &allocationInfo);
	if (result != VK_SUCCESS) {
		Log::Error("Vulkan", "Failed to create buffer: {}", vk::to_string(vk::Result(result)));
		return BufferHandle();
	}
	Log::Trace("Vulkan", "Buffer created.");

	const bool mappable(_gpuInfo.Memory.memoryTypes[allocationInfo.memoryType].propertyFlags &
	                    vk::MemoryPropertyFlagBits::eHostVisible);
	void* mappedMemory = allocationInfo.pMappedData;
	if (mappable && !mappedMemory) { vmaMapMemory(_allocator, allocation, &mappedMemory); }

	BufferHandle handle(_bufferPool.Allocate(*this, buffer, allocation, actualCI, mappedMemory));

	if (initialData) {
		if (mappedMemory) {
			memcpy(mappedMemory, initialData, actualCI.Size);
		} else {
			auto stagingCI   = actualCI;
			stagingCI.Domain = BufferDomain::Host;
			auto staging     = CreateBuffer(stagingCI, initialData);

			auto copyCmd = RequestCommandBuffer(CommandBufferType::AsyncTransfer);
			copyCmd->CopyBuffer(*handle, *staging);

			DeviceLock();
			SubmitStaging(copyCmd, actualCI.Usage, true);
		}
	}

	return handle;
}

ImageHandle Device::CreateImage(const ImageCreateInfo& imageCI, const ImageInitialData* initialData) {
	struct ImageInitialBuffer {
		BufferHandle Buffer;
		std::vector<vk::BufferImageCopy> Blits;
	};
	const bool generateMips = imageCI.MiscFlags & ImageCreateFlagBits::GenerateMipmaps;

	// Create our image staging buffer, if applicable.
	ImageInitialBuffer initialBuffer;
	if (initialData) {
		uint32_t copyLevels;
		if (generateMips) {
			copyLevels = 1;
		} else if (imageCI.MipLevels == 0) {
			copyLevels = TextureFormatLayout::MipLevels(imageCI.Width, imageCI.Height, imageCI.Depth);
		} else {
			copyLevels = imageCI.MipLevels;
		}

		TextureFormatLayout layout;
		switch (imageCI.Type) {
			case vk::ImageType::e1D:
				layout.Set1D(imageCI.Format, imageCI.Width, imageCI.ArrayLayers, copyLevels);
				break;
			case vk::ImageType::e2D:
				layout.Set2D(imageCI.Format, imageCI.Width, imageCI.Height, imageCI.ArrayLayers, copyLevels);
				break;
			case vk::ImageType::e3D:
				layout.Set3D(imageCI.Format, imageCI.Width, imageCI.Height, imageCI.Depth, copyLevels);
				break;
			default:
				return {};
		}

		const BufferCreateInfo bufferCI(
			BufferDomain::Host, layout.GetRequiredSize(), vk::BufferUsageFlagBits::eTransferSrc);
		initialBuffer.Buffer = CreateBuffer(bufferCI);
		uint8_t* data        = reinterpret_cast<uint8_t*>(initialBuffer.Buffer->Map());

		uint32_t index = 0;
		layout.SetBuffer(data, layout.GetRequiredSize());
		for (uint32_t level = 0; level < copyLevels; ++level) {
			const auto& mipInfo            = layout.GetMipInfo(level);
			const uint32_t dstHeightStride = layout.GetLayerSize(level);
			const size_t rowSize           = layout.GetRowSize(level);

			for (uint32_t layer = 0; layer < imageCI.ArrayLayers; ++layer, ++index) {
				const uint32_t srcRowLength = initialData[index].RowLength ? initialData[index].RowLength : mipInfo.RowLength;
				const uint32_t srcArrayHeight =
					initialData[index].ImageHeight ? initialData[index].ImageHeight : mipInfo.ImageHeight;
				const uint32_t srcRowStride    = layout.RowByteStride(srcRowLength);
				const uint32_t srcHeightStride = layout.LayerByteStride(srcArrayHeight, srcRowStride);

				uint8_t* dst       = static_cast<uint8_t*>(layout.Data(layer, level));
				const uint8_t* src = static_cast<const uint8_t*>(initialData[index].Data);

				for (uint32_t z = 0; z < mipInfo.Depth; ++z) {
					for (uint32_t y = 0; y < mipInfo.BlockImageHeight; ++y) {
						memcpy(dst + z * dstHeightStride + y * rowSize, src + z * srcHeightStride + y * srcRowStride, rowSize);
					}
				}
			}
		}
		initialBuffer.Blits = layout.BuildBufferImageCopies();
	}

	vk::ImageCreateInfo imageCreate(imageCI.Flags,
	                                imageCI.Type,
	                                imageCI.Format,
	                                vk::Extent3D(imageCI.Width, imageCI.Height, imageCI.Depth),
	                                imageCI.MipLevels,
	                                imageCI.ArrayLayers,
	                                imageCI.Samples,
	                                vk::ImageTiling::eOptimal,
	                                imageCI.Usage,
	                                vk::SharingMode::eExclusive,
	                                nullptr,
	                                vk::ImageLayout::eUndefined);
	if (imageCI.Domain == ImageDomain::Transient) { imageCreate.usage |= vk::ImageUsageFlagBits::eTransientAttachment; }
	if (initialData) { imageCreate.usage |= vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst; }
	if (imageCreate.mipLevels == 0) {
		imageCreate.mipLevels = TextureFormatLayout::MipLevels(imageCI.Width, imageCI.Height, imageCI.Depth);
	}

	bool createUnormSrgbViews = false;
	if (imageCI.MiscFlags & ImageCreateFlagBits::MutableSrgb) {
		Log::Warning("Vulkan", "ImageCreateFlagBits::MutableSrgb not yet supported!");
	}

	if ((imageCreate.usage & vk::ImageUsageFlagBits::eStorage) ||
	    (imageCI.MiscFlags & ImageCreateFlagBits::MutableSrgb)) {
		imageCreate.flags |= vk::ImageCreateFlagBits::eMutableFormat;
	}

	ImageCreateFlags queueFlags =
		imageCI.MiscFlags &
		(ImageCreateFlagBits::ConcurrentQueueGraphics | ImageCreateFlagBits::ConcurrentQueueAsyncCompute |
	   ImageCreateFlagBits::ConcurrentQueueAsyncGraphics | ImageCreateFlagBits::ConcurrentQueueAsyncTransfer);
	bool concurrentQueue(queueFlags);
	std::vector<uint32_t> families;
	if (concurrentQueue) {
		std::set<uint32_t> uniqueFamilies;
		if (queueFlags &
		    (ImageCreateFlagBits::ConcurrentQueueGraphics | ImageCreateFlagBits::ConcurrentQueueAsyncGraphics)) {
			uniqueFamilies.insert(_queues.Family(QueueType::Graphics));
		}
		if (queueFlags & ImageCreateFlagBits::ConcurrentQueueAsyncCompute) {
			uniqueFamilies.insert(_queues.Family(QueueType::Compute));
		}
		if (initialData || (queueFlags & ImageCreateFlagBits::ConcurrentQueueAsyncTransfer)) {
			uniqueFamilies.insert(_queues.Family(QueueType::Transfer));
		}
		if (initialData) { uniqueFamilies.insert(_queues.Family(QueueType::Graphics)); }

		if (uniqueFamilies.size() > 1) {
			families = std::vector<uint32_t>(uniqueFamilies.begin(), uniqueFamilies.end());

			imageCreate.sharingMode = vk::SharingMode::eConcurrent;
			imageCreate.setQueueFamilyIndices(families);
		}
	}

	const VmaAllocationCreateInfo imageAI = {.usage = VMA_MEMORY_USAGE_AUTO};

	VkImage image;
	VmaAllocation allocation;
	VmaAllocationInfo allocationInfo;
	const auto result = vmaCreateImage(_allocator,
	                                   reinterpret_cast<const VkImageCreateInfo*>(&imageCreate),
	                                   &imageAI,
	                                   &image,
	                                   &allocation,
	                                   &allocationInfo);
	if (result != VK_SUCCESS) {
		Log::Error("Vulkan", "Failed to create image: {}", vk::to_string(vk::Result(result)));
		return ImageHandle();
	}
	Log::Trace("Vulkan", "Image created.");

	auto tmpCI      = imageCI;
	tmpCI.Usage     = imageCreate.usage;
	tmpCI.MipLevels = imageCreate.mipLevels;

	const bool hasView(imageCreate.usage &
	                   (vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage |
	                    vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eDepthStencilAttachment |
	                    vk::ImageUsageFlagBits::eInputAttachment));
	vk::ImageViewType viewType = {};
	vk::ImageView imageView;
	vk::ImageView depthView;
	vk::ImageView stencilView;
	vk::ImageView unormView;
	vk::ImageView srgbView;
	std::vector<vk::ImageView> renderTargetViews;
	if (hasView) {
		vk::ImageViewCreateInfo defaultViewCI(
			{},
			image,
			tmpCI.GetImageViewType(),
			imageCI.Format,
			vk::ComponentMapping(),
			vk::ImageSubresourceRange(FormatToAspect(tmpCI.Format), 0, tmpCI.MipLevels, 0, tmpCI.ArrayLayers));
		viewType = defaultViewCI.viewType;

		imageView = _device.createImageView(defaultViewCI);
		Log::Trace("Vulkan", "Image View created.");

		// Alt views
		if (defaultViewCI.viewType != vk::ImageViewType::eCube && defaultViewCI.viewType != vk::ImageViewType::eCubeArray &&
		    defaultViewCI.viewType != vk::ImageViewType::e3D) {
			if (defaultViewCI.subresourceRange.aspectMask ==
			    (vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil)) {
				if ((tmpCI.Usage & ~vk::ImageUsageFlagBits::eDepthStencilAttachment)) {
					auto viewCI = defaultViewCI;

					viewCI.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth;
					depthView                          = _device.createImageView(viewCI);
					Log::Trace("Vulkan", "Image View created.");

					viewCI.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eStencil;
					stencilView                        = _device.createImageView(viewCI);
					Log::Trace("Vulkan", "Image View created.");
				}
			}
		}

		// Render target views
		if (defaultViewCI.viewType != vk::ImageViewType::e3D) {
			renderTargetViews.reserve(defaultViewCI.subresourceRange.layerCount);

			if ((tmpCI.Usage &
			     (vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eDepthStencilAttachment)) &&
			    ((defaultViewCI.subresourceRange.levelCount > 1) || (defaultViewCI.subresourceRange.layerCount > 1))) {
				auto viewCI                        = defaultViewCI;
				viewCI.viewType                    = vk::ImageViewType::e2D;
				viewCI.subresourceRange.levelCount = 1;
				viewCI.subresourceRange.layerCount = 1;

				for (uint32_t layer = 0; layer < defaultViewCI.subresourceRange.layerCount; ++layer) {
					viewCI.subresourceRange.baseArrayLayer = layer + defaultViewCI.subresourceRange.baseArrayLayer;
					renderTargetViews.push_back(_device.createImageView(viewCI));
					Log::Trace("Vulkan", "Image View created.");
				}
			}
		}
	}

	ImageHandle handle(_imagePool.Allocate(*this, image, imageView, allocation, tmpCI, viewType));
	if (handle) {
		imageView = nullptr;
		if (hasView) {
			handle->GetView().SetAltViews(depthView, stencilView);
			handle->GetView().SetSrgbView(srgbView);
			handle->GetView().SetUnormView(unormView);
			handle->GetView().SetRenderTargetViews(renderTargetViews);

			depthView   = nullptr;
			stencilView = nullptr;
			unormView   = nullptr;
			srgbView    = nullptr;
			renderTargetViews.clear();
		}
	}

	for (auto& view : renderTargetViews) { _device.destroyImageView(view); }
	if (srgbView) { _device.destroyImageView(srgbView); }
	if (unormView) { _device.destroyImageView(unormView); }
	if (stencilView) { _device.destroyImageView(stencilView); }
	if (depthView) { _device.destroyImageView(depthView); }
	if (imageView) { _device.destroyImageView(imageView); }

	const bool shareCompute = (queueFlags & ImageCreateFlagBits::ConcurrentQueueAsyncCompute) &&
	                          !_queues.SameQueue(QueueType::Graphics, QueueType::Compute);
	const bool shareAsyncGraphics = GetQueueType(CommandBufferType::AsyncGraphics) == QueueType::Compute &&
	                                (queueFlags & ImageCreateFlagBits::ConcurrentQueueAsyncGraphics);

	CommandBufferHandle transitionCmd;
	if (initialData) {
		vk::AccessFlags finalTransitionSrcAccess = {};
		if (generateMips) {
			finalTransitionSrcAccess = vk::AccessFlagBits::eTransferRead;
		} else if (_queues.SameQueue(QueueType::Graphics, QueueType::Transfer)) {
			finalTransitionSrcAccess = vk::AccessFlagBits::eTransferWrite;
		}

		vk::AccessFlags prepareSrcAccess = _queues.SameQueue(QueueType::Graphics, QueueType::Transfer)
		                                     ? vk::AccessFlagBits::eTransferWrite
		                                     : vk::AccessFlags{};
		bool needMipmapBarrier           = true;
		bool needInitialBarrier          = true;

		auto graphicsCmd = RequestCommandBuffer(CommandBufferType::Generic);
		CommandBufferHandle transferCmd;
		if (!_queues.SameQueue(QueueType::Transfer, QueueType::Graphics)) {
			transferCmd = RequestCommandBuffer(CommandBufferType::AsyncTransfer);
		} else {
			transferCmd = graphicsCmd;
		}

		transferCmd->ImageBarrier(*handle,
		                          vk::ImageLayout::eUndefined,
		                          vk::ImageLayout::eTransferDstOptimal,
		                          vk::PipelineStageFlagBits::eTopOfPipe,
		                          {},
		                          vk::PipelineStageFlagBits::eTransfer,
		                          vk::AccessFlagBits::eTransferWrite);
		transferCmd->CopyBufferToImage(*handle, *initialBuffer.Buffer, initialBuffer.Blits);

		if (!_queues.SameQueue(QueueType::Transfer, QueueType::Graphics)) {
			vk::PipelineStageFlags dstStages = generateMips ? vk::PipelineStageFlagBits::eTransfer : handle->GetStageFlags();

			if (!concurrentQueue && !_queues.SameFamily(QueueType::Transfer, QueueType::Graphics)) {
				needMipmapBarrier = false;

				vk::ImageMemoryBarrier release(
					vk::AccessFlagBits::eTransferWrite,
					{},
					vk::ImageLayout::eTransferDstOptimal,
					{},
					_queues.Family(QueueType::Transfer),
					_queues.Family(QueueType::Graphics),
					handle->GetImage(),
					vk::ImageSubresourceRange(FormatToAspect(imageCreate.format), 0, 0, 0, imageCreate.arrayLayers));
				if (generateMips) {
					release.newLayout                   = vk::ImageLayout::eTransferSrcOptimal;
					release.subresourceRange.levelCount = 1;
				} else {
					release.newLayout                   = imageCI.InitialLayout;
					release.subresourceRange.levelCount = imageCreate.mipLevels;
					needInitialBarrier                  = false;
				}

				auto acquire          = release;
				acquire.srcAccessMask = {};
				if (generateMips) {
					acquire.dstAccessMask = vk::AccessFlagBits::eTransferRead;
				} else {
					acquire.dstAccessMask = handle->GetAccessFlags() & ImageLayoutToAccess(imageCI.InitialLayout);
				}

				transferCmd->Barrier(
					vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eBottomOfPipe, {}, {}, {release});
				graphicsCmd->Barrier(dstStages, dstStages, {}, {}, {acquire});
			}

			std::vector<SemaphoreHandle> semaphores(1);
			Submit(transferCmd, nullptr, &semaphores);
			AddWaitSemaphore(CommandBufferType::Generic, semaphores[0], dstStages, true);
		}

		if (generateMips) {
			graphicsCmd->MipmapBarrier(*handle,
			                           vk::ImageLayout::eTransferDstOptimal,
			                           vk::PipelineStageFlagBits::eTransfer,
			                           prepareSrcAccess,
			                           needMipmapBarrier);
			graphicsCmd->GenerateMipmaps(*handle);
		}

		if (needInitialBarrier) {
			graphicsCmd->ImageBarrier(
				*handle,
				generateMips ? vk::ImageLayout::eTransferSrcOptimal : vk::ImageLayout::eTransferDstOptimal,
				imageCI.InitialLayout,
				vk::PipelineStageFlagBits::eTransfer,
				finalTransitionSrcAccess,
				handle->GetStageFlags(),
				handle->GetAccessFlags() & ImageLayoutToAccess(imageCI.InitialLayout));
		}

		transitionCmd = std::move(graphicsCmd);
	} else if (imageCI.InitialLayout != vk::ImageLayout::eUndefined) {
		auto cmd = RequestCommandBuffer(CommandBufferType::Generic);
		cmd->ImageBarrier(*handle,
		                  imageCreate.initialLayout,
		                  imageCI.InitialLayout,
		                  vk::PipelineStageFlagBits::eTopOfPipe,
		                  {},
		                  handle->GetStageFlags(),
		                  handle->GetAccessFlags() & ImageLayoutToAccess(imageCI.InitialLayout));
		transitionCmd = std::move(cmd);
	}

	if (transitionCmd) {
		if (shareCompute || shareAsyncGraphics) {
			std::vector<SemaphoreHandle> semaphores(1);
			Submit(transitionCmd, nullptr, &semaphores);
			auto dstStages = handle->GetStageFlags();
			if (!_queues.SameFamily(QueueType::Graphics, QueueType::Compute)) {
				dstStages &= vk::PipelineStageFlagBits::eComputeShader | vk::PipelineStageFlagBits::eTransfer;
			}
			AddWaitSemaphore(CommandBufferType::AsyncCompute, semaphores[0], dstStages, true);
		} else {
			DeviceLock();
			SubmitNoLock(transitionCmd, nullptr, nullptr);
			if (concurrentQueue) { FlushFrame(QueueType::Graphics); }
		}
	}

	return handle;
}

CommandBufferHandle Device::RequestCommandBuffer(CommandBufferType type) {
	return RequestCommandBufferForThread(GetThreadIndex(), type);
}

CommandBufferHandle Device::RequestCommandBufferForThread(uint32_t threadIndex, CommandBufferType type) {
	DeviceLock();
	return RequestCommandBufferNoLock(threadIndex, type);
}

uint64_t Device::AllocateCookie() {
#ifdef LUNA_VULKAN_MT
	return _cookie.fetch_add(16, std::memory_order_relaxed) + 16;
#else
	_cookie += 16;
	return _cookie;
#endif
}

void Device::AddWaitSemaphore(CommandBufferType cbType,
                              SemaphoreHandle semaphore,
                              vk::PipelineStageFlags stages,
                              bool flush) {
	DeviceLock();
	AddWaitSemaphoreNoLock(GetQueueType(cbType), std::move(semaphore), stages, flush);
}

void Device::Submit(CommandBufferHandle cmd, FenceHandle* fence, std::vector<SemaphoreHandle>* semaphores) {
	DeviceLock();
	SubmitNoLock(std::move(cmd), fence, semaphores);
}

void Device::WaitIdle() {
	DeviceFlush();
	WaitIdleNoLock();
}

vk::Fence Device::AllocateFence() {
	if (_availableFences.empty()) {
		const vk::FenceCreateInfo fenceCI;
		auto fence = _device.createFence(fenceCI);

		Log::Trace("Vulkan", "Fence created.");

		return fence;
	}

	auto fence = _availableFences.back();
	_availableFences.pop_back();

	return fence;
}

vk::Semaphore Device::AllocateSemaphore() {
	if (_availableSemaphores.empty()) {
		const vk::SemaphoreCreateInfo semaphoreCI;
		auto semaphore = _device.createSemaphore(semaphoreCI);

		Log::Trace("Vulkan", "Semaphore created.");

		return semaphore;
	}

	auto semaphore = _availableSemaphores.back();
	_availableSemaphores.pop_back();

	return semaphore;
}

void Device::CreateTimelineSemaphores() {
	if (!_gpuInfo.AvailableFeatures.TimelineSemaphore.timelineSemaphore) { return; }

	const vk::SemaphoreCreateInfo semaphoreCI;
	const vk::SemaphoreTypeCreateInfo semaphoreType(vk::SemaphoreType::eTimeline, 0);
	const vk::StructureChain chain(semaphoreCI, semaphoreType);
	for (auto& queue : _queueData) {
		queue.TimelineSemaphore = _device.createSemaphore(chain.get());
		queue.TimelineValue     = 0;
	}
}

void Device::DestroyTimelineSemaphores() {
	if (!_gpuInfo.AvailableFeatures.TimelineSemaphore.timelineSemaphore) { return; }

	for (auto& queue : _queueData) {
		if (queue.TimelineSemaphore) {
			_device.destroySemaphore(queue.TimelineSemaphore);
			queue.TimelineSemaphore = VK_NULL_HANDLE;
		}
	}
}

Device::FrameContext& Device::Frame() {
	return *_frameContexts[_currentFrameContext];
}

QueueType Device::GetQueueType(CommandBufferType cbType) const {
	if (cbType != CommandBufferType::AsyncGraphics) {
		return static_cast<QueueType>(cbType);
	} else {
		if (_queues.SameFamily(QueueType::Graphics, QueueType::Compute) &&
		    !_queues.SameQueue(QueueType::Graphics, QueueType::Compute)) {
			return QueueType::Compute;
		} else {
			return QueueType::Graphics;
		}
	}
}

void Device::ReleaseFence(vk::Fence fence) {
	_availableFences.push_back(fence);
}

void Device::ReleaseSemaphore(vk::Semaphore semaphore) {
	_availableSemaphores.push_back(semaphore);
}

void Device::DestroyBuffer(vk::Buffer buffer) {
	DeviceLock();
	DestroyBufferNoLock(buffer);
}

void Device::DestroyImage(vk::Image image) {
	DeviceLock();
	DestroyImageNoLock(image);
}

void Device::DestroyImageView(vk::ImageView view) {
	DeviceLock();
	DestroyImageViewNoLock(view);
}

void Device::DestroySemaphore(vk::Semaphore semaphore) {
	DeviceLock();
	DestroySemaphoreNoLock(semaphore);
}

void Device::FreeMemory(const VmaAllocation& allocation) {
	DeviceLock();
	FreeMemoryNoLock(allocation);
}

void Device::RecycleSemaphore(vk::Semaphore semaphore) {
	DeviceLock();
	RecycleSemaphoreNoLock(semaphore);
}

void Device::ResetFence(vk::Fence fence, bool observedWait) {
	DeviceLock();
	ResetFenceNoLock(fence, observedWait);
}

void Device::DestroyBufferNoLock(vk::Buffer buffer) {
	Frame().BuffersToDestroy.push_back(buffer);
}

void Device::DestroyImageNoLock(vk::Image image) {
	Frame().ImagesToDestroy.push_back(image);
}

void Device::DestroyImageViewNoLock(vk::ImageView view) {
	Frame().ImageViewsToDestroy.push_back(view);
}

void Device::DestroySemaphoreNoLock(vk::Semaphore semaphore) {
	Frame().SemaphoresToDestroy.push_back(semaphore);
}

void Device::FreeMemoryNoLock(const VmaAllocation& allocation) {
	Frame().MemoryToFree.push_back(allocation);
}

void Device::RecycleSemaphoreNoLock(vk::Semaphore semaphore) {
	Frame().SemaphoresToRecycle.push_back(semaphore);
}

void Device::ResetFenceNoLock(vk::Fence fence, bool observedWait) {
	if (observedWait) {
		_device.resetFences(fence);
		ReleaseFence(fence);
	} else {
		Frame().FencesToRecycle.push_back(fence);
	}
}

CommandBufferHandle Device::RequestCommandBufferNoLock(uint32_t threadIndex, CommandBufferType type) {
	const auto queueType = GetQueueType(type);
	auto& pool           = Frame().CommandPools[int(queueType)][threadIndex];
	auto cmd             = pool->RequestCommandBuffer();

	const vk::CommandBufferBeginInfo cmdBI(vk::CommandBufferUsageFlagBits::eOneTimeSubmit, nullptr);
	cmd.begin(cmdBI);
	++_lock.Counter;

	CommandBufferHandle handle(_commandBufferPool.Allocate(*this, cmd, type, threadIndex));

	return handle;
}

void Device::AddWaitSemaphoreNoLock(QueueType queueType,
                                    SemaphoreHandle semaphore,
                                    vk::PipelineStageFlags stages,
                                    bool flush) {
	if (flush) { FlushFrame(queueType); }

	auto& data = _queueData[int(queueType)];

	semaphore->SignalPendingWait();
	data.WaitSemaphores.push_back(semaphore);
	data.WaitStages.push_back(stages);
	data.NeedsFence = true;
}

void Device::EndFrameNoLock() {
	InternalFence fence;

	for (const auto type : QueueFlushOrder) {
		if (_queueData[int(type)].NeedsFence || !Frame().Submissions[int(type)].empty()) {
			SubmitQueue(type, &fence, nullptr);
			if (fence.Fence) {
				Frame().FencesToAwait.push_back(fence.Fence);
				Frame().FencesToRecycle.push_back(fence.Fence);
			}
			_queueData[int(type)].NeedsFence = false;
		}
	}
}

void Device::FlushFrame(QueueType queueType) {
	if (!_queues.Queue(queueType)) { return; }

	SubmitQueue(queueType, nullptr, nullptr);
}

void Device::SubmitNoLock(CommandBufferHandle cmd, FenceHandle* fence, std::vector<SemaphoreHandle>* semaphores) {
	const auto cbType    = cmd->GetType();
	const auto queueType = GetQueueType(cbType);
	auto& submissions    = Frame().Submissions[int(queueType)];

	cmd->End();
	submissions.push_back(std::move(cmd));

	InternalFence internalFence;
	if (fence || semaphores) { SubmitQueue(queueType, fence ? &internalFence : nullptr, semaphores); }

	if (fence) {
		if (internalFence.TimelineValue) {
			*fence = FenceHandle(_fencePool.Allocate(*this, internalFence.TimelineSemaphore, internalFence.TimelineValue));
		} else {
			*fence = FenceHandle(_fencePool.Allocate(*this, internalFence.Fence));
		}
	}

	_lock.Counter--;
#ifdef LUNA_VULKAN_MT
	_lock.Condition.notify_all();
#endif
}

void Device::SubmitQueue(QueueType queueType, InternalFence* submitFence, std::vector<SemaphoreHandle>* semaphores) {
	auto& queueData          = _queueData[static_cast<int>(queueType)];
	auto& submissions        = Frame().Submissions[static_cast<int>(queueType)];
	const bool hasSemaphores = semaphores != nullptr && semaphores->size() != 0;

	if (submissions.empty() && submitFence == nullptr && !hasSemaphores) { return; }

	if (queueType != QueueType::Transfer) { FlushFrame(QueueType::Transfer); }

	vk::Queue queue                                     = _queues.Queue(queueType);
	vk::Semaphore timelineSemaphore                     = queueData.TimelineSemaphore;
	uint64_t timelineValue                              = ++queueData.TimelineValue;
	Frame().TimelineValues[static_cast<int>(queueType)] = timelineValue;

	// Batch all of our command buffers into as few submissions as possible. Increment batch whenever we need to use a
	// signal semaphore.
	constexpr static const int MaxSubmissions = 8;
	struct SubmitBatch {
		bool HasTimeline = false;
		std::vector<vk::CommandBuffer> CommandBuffers;
		std::vector<vk::Semaphore> SignalSemaphores;
		std::vector<uint64_t> SignalValues;
		std::vector<vk::Semaphore> WaitSemaphores;
		std::vector<vk::PipelineStageFlags> WaitStages;
		std::vector<uint64_t> WaitValues;
	};
	std::array<SubmitBatch, MaxSubmissions> batches;
	uint8_t batch = 0;

	// First, add all of the wait semaphores we've accumulated over the frame to the first batch. These usually come from
	// inter-queue staging buffers.
	for (size_t i = 0; i < queueData.WaitSemaphores.size(); ++i) {
		auto& semaphoreHandle = queueData.WaitSemaphores[i];
		auto semaphore        = semaphoreHandle->Consume();
		auto waitStages       = queueData.WaitStages[i];
		auto waitValue        = semaphoreHandle->GetTimelineValue();

		batches[batch].WaitSemaphores.push_back(semaphore);
		batches[batch].WaitStages.push_back(waitStages);
		batches[batch].WaitValues.push_back(waitValue);
		batches[batch].HasTimeline = batches[batch].HasTimeline || waitValue != 0;
	}
	queueData.WaitSemaphores.clear();
	queueData.WaitStages.clear();

	// Add our command buffers.
	for (auto& cmdBufHandle : submissions) {
		const vk::PipelineStageFlags swapchainStages = cmdBufHandle->GetSwapchainStages();

		/*
		if (swapchainStages && !_swapchainAcquireConsumed) {
		  if (_swapchainAcquire && _swapchainAcquire->GetSemaphore()) {
		    if (!batches[batch].CommandBuffers.empty() || !batches[batch].SignalSemaphores.empty()) { ++batch; }

		    const auto value = _swapchainAcquire->GetTimelineValue();
		    batches[batch].WaitSemaphores.push_back(_swapchainAcquire->GetSemaphore());
		    batches[batch].WaitStages.push_back(swapchainStages);
		    batches[batch].WaitValues.push_back(value);

		    if (!value) { Frame().SemaphoresToRecycle.push_back(_swapchainAcquire->GetSemaphore()); }

		    _swapchainAcquire->Consume();
		    _swapchainAcquireConsumed = true;
		    _swapchainAcquire.Reset();
		  }

		  if (!batches[batch].SignalSemaphores.empty()) {
		    ++batch;
		    assert(batch < MaxSubmissions);
		  }

		  batches[batch].CommandBuffers.push_back(cmdBufHandle->GetCommandBuffer());

		  vk::Semaphore release            = AllocateSemaphore();
		  _swapchainRelease                = SemaphoreHandle(_semaphorePool.Allocate(*this, release, true));
		  _swapchainRelease->_internalSync = true;
		  batches[batch].SignalSemaphores.push_back(release);
		  batches[batch].SignalValues.push_back(0);
		} else {
		*/
		if (!batches[batch].SignalSemaphores.empty()) {
			++batch;
			assert(batch < MaxSubmissions);
		}

		batches[batch].CommandBuffers.push_back(cmdBufHandle->GetCommandBuffer());
		/*
	}
	*/
	}
	submissions.clear();

	// Only use a fence if we have to. Prefer using the timeline semaphore for each queue.
	vk::Fence fence = VK_NULL_HANDLE;
	if (submitFence && !_gpuInfo.AvailableFeatures.TimelineSemaphore.timelineSemaphore) {
		fence              = AllocateFence();
		submitFence->Fence = fence;
	}

	// Emit any necessary semaphores from the final batch.
	if (_gpuInfo.AvailableFeatures.TimelineSemaphore.timelineSemaphore) {
		batches[batch].SignalSemaphores.push_back(timelineSemaphore);
		batches[batch].SignalValues.push_back(timelineValue);
		batches[batch].HasTimeline = true;

		if (submitFence) {
			submitFence->Fence             = VK_NULL_HANDLE;
			submitFence->TimelineSemaphore = timelineSemaphore;
			submitFence->TimelineValue     = timelineValue;
		}

		if (hasSemaphores) {
			for (size_t i = 0; i < semaphores->size(); ++i) {
				(*semaphores)[i] = SemaphoreHandle(_semaphorePool.Allocate(*this, timelineSemaphore, timelineValue));
			}
		}
	} else {
		if (submitFence) {
			submitFence->TimelineSemaphore = VK_NULL_HANDLE;
			submitFence->TimelineValue     = 0;
		}

		if (hasSemaphores) {
			for (size_t i = 0; i < semaphores->size(); ++i) {
				vk::Semaphore sem = AllocateSemaphore();
				batches[batch].SignalSemaphores.push_back(sem);
				batches[batch].SignalValues.push_back(0);
				(*semaphores)[i] = SemaphoreHandle(_semaphorePool.Allocate(*this, sem, true));
			}
		}
	}

	// Build our submit info structures.
	std::array<vk::SubmitInfo, MaxSubmissions> submits;
	std::array<vk::TimelineSemaphoreSubmitInfo, MaxSubmissions> timelineSubmits;
	for (uint8_t i = 0; i <= batch; ++i) {
		submits[i] = vk::SubmitInfo(
			batches[i].WaitSemaphores, batches[i].WaitStages, batches[i].CommandBuffers, batches[i].SignalSemaphores);
		if (batches[i].HasTimeline) {
			timelineSubmits[i] = vk::TimelineSemaphoreSubmitInfo(batches[i].WaitValues, batches[i].SignalValues);
			submits[i].pNext   = &timelineSubmits[i];
		}
	}

	// Compact our submissions to remove any empty ones.
	uint32_t submitCount = 0;
	for (size_t i = 0; i < submits.size(); ++i) {
		if (submits[i].waitSemaphoreCount || submits[i].commandBufferCount || submits[i].signalSemaphoreCount) {
			if (i != submitCount) { submits[submitCount] = submits[i]; }
			++submitCount;
		}
	}

	// Finally, submit it all!
	const auto submitResult = queue.submit(submitCount, submits.data(), fence);
	if (submitResult != vk::Result::eSuccess) {
		Log::Error("Vulkan::Device", "Error occurred when submitting command buffers: {}", vk::to_string(submitResult));
	}

	// If we weren't able to use a timeline semaphore, we need to make sure there is a fence in
	// place to wait for completion.
	if (!_gpuInfo.AvailableFeatures.TimelineSemaphore.timelineSemaphore) { queueData.NeedsFence = true; }
}

void Device::SubmitStaging(CommandBufferHandle& cmd, vk::BufferUsageFlags usage, bool flush) {
	const auto access  = BufferUsageToAccess(usage);
	const auto stages  = BufferUsageToStages(usage);
	vk::Queue srcQueue = _queues.Queue(GetQueueType(cmd->GetType()));

	if (srcQueue == _queues.Queue(QueueType::Graphics) && srcQueue == _queues.Queue(QueueType::Compute)) {
		cmd->Barrier(vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferWrite, stages, access);
		SubmitNoLock(cmd, nullptr, nullptr);
	} else {
		const auto computeStages =
			stages & (vk::PipelineStageFlagBits::eComputeShader | vk::PipelineStageFlagBits::eTransfer |
		            vk::PipelineStageFlagBits::eDrawIndirect);
		const auto computeAccess  = access & (vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite |
                                         vk::AccessFlagBits::eTransferRead | vk::AccessFlagBits::eUniformRead |
                                         vk::AccessFlagBits::eTransferWrite | vk::AccessFlagBits::eIndirectCommandRead);
		const auto graphicsStages = stages;

		if (srcQueue == _queues.Queue(QueueType::Graphics)) {
			cmd->Barrier(vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferWrite, graphicsStages, access);

			if (bool(computeStages)) {
				std::vector<SemaphoreHandle> semaphores(1);
				SubmitNoLock(cmd, nullptr, &semaphores);
				AddWaitSemaphoreNoLock(QueueType::Compute, semaphores[0], computeStages, flush);
			} else {
				SubmitNoLock(cmd, nullptr, nullptr);
			}
		} else if (srcQueue == _queues.Queue(QueueType::Compute)) {
			cmd->Barrier(
				vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferWrite, computeStages, computeAccess);

			if (bool(graphicsStages)) {
				std::vector<SemaphoreHandle> semaphores(1);
				SubmitNoLock(cmd, nullptr, &semaphores);
				AddWaitSemaphoreNoLock(QueueType::Graphics, semaphores[0], graphicsStages, flush);
			} else {
				SubmitNoLock(cmd, nullptr, nullptr);
			}
		} else {
			if (bool(graphicsStages) && bool(computeStages)) {
				std::vector<SemaphoreHandle> semaphores(2);
				SubmitNoLock(cmd, nullptr, &semaphores);
				AddWaitSemaphoreNoLock(QueueType::Graphics, semaphores[0], graphicsStages, flush);
				AddWaitSemaphoreNoLock(QueueType::Compute, semaphores[1], computeStages, flush);
			} else if (bool(graphicsStages)) {
				std::vector<SemaphoreHandle> semaphores(1);
				SubmitNoLock(cmd, nullptr, &semaphores);
				AddWaitSemaphoreNoLock(QueueType::Graphics, semaphores[0], graphicsStages, flush);
			} else if (bool(computeStages)) {
				std::vector<SemaphoreHandle> semaphores(1);
				SubmitNoLock(cmd, nullptr, &semaphores);
				AddWaitSemaphoreNoLock(QueueType::Compute, semaphores[0], computeStages, flush);
			} else {
				SubmitNoLock(cmd, nullptr, nullptr);
			}
		}
	}
}

void Device::WaitIdleNoLock() {
	if (!_frameContexts.empty()) { EndFrameNoLock(); }

	_device.waitIdle();

	for (auto& queue : _queueData) {
		for (auto& semaphore : queue.WaitSemaphores) { _device.destroySemaphore(semaphore->Consume()); }
		queue.WaitSemaphores.clear();
		queue.WaitStages.clear();
	}

	for (auto& frame : _frameContexts) {
		frame->FencesToAwait.clear();
		frame->Begin();
	}
}

Device::FrameContext::FrameContext(Device& device, uint32_t index) : Parent(device), Index(index) {
	const auto threadCount = 1;
	for (int i = 0; i < QueueTypeCount; ++i) {
		CommandPools[i].reserve(threadCount);
		TimelineSemaphores[i] = device._queueData[i].TimelineSemaphore;
		TimelineValues[i]     = device._queueData[i].TimelineValue;
		for (int j = 0; j < threadCount; ++j) {
			CommandPools[i].emplace_back(std::make_unique<CommandPool>(Parent, Parent._queues.Families[i], false));
		}
	}
}

Device::FrameContext::~FrameContext() noexcept {
	Begin();
}

void Device::FrameContext::Begin() {
	auto device = Parent._device;

	// Wait on our timeline semaphores to ensure this frame context has completed all of its pending work.
	{
		bool hasTimelineSemaphores = true;
		for (auto& queue : Parent._queueData) {
			if (!queue.TimelineSemaphore) {
				hasTimelineSemaphores = false;
				break;
			}
		}
		if (hasTimelineSemaphores) {
			uint32_t semaphoreCount = 0;
			std::array<vk::Semaphore, QueueTypeCount> semaphores;
			std::array<uint64_t, QueueTypeCount> values;
			for (size_t i = 0; i < QueueTypeCount; ++i) {
				if (TimelineValues[i]) {
					semaphores[semaphoreCount] = Parent._queueData[i].TimelineSemaphore;
					values[semaphoreCount]     = TimelineValues[i];
					++semaphoreCount;
				}
			}

			if (semaphoreCount) {
				const vk::SemaphoreWaitInfo waitInfo({}, semaphoreCount, semaphores.data(), values.data());
				const auto waitResult = device.waitSemaphoresKHR(waitInfo, std::numeric_limits<uint64_t>::max());
				if (waitResult != vk::Result::eSuccess) {
					Log::Error("Vulkan::Device", "Failed to wait on timeline semaphores!");
				}
			}
		}
	}

	if (!FencesToAwait.empty()) {
		const auto waitResult = device.waitForFences(FencesToAwait, VK_TRUE, std::numeric_limits<uint64_t>::max());
		if (waitResult != vk::Result::eSuccess) { Log::Error("Vulkan", "Failed to await frame fences!"); }
		FencesToAwait.clear();
	}

	if (!FencesToRecycle.empty()) {
		device.resetFences(FencesToRecycle);
		for (auto& fence : FencesToRecycle) { Parent.ReleaseFence(fence); }
		FencesToRecycle.clear();
	}

	for (auto& pools : CommandPools) {
		for (auto& pool : pools) { pool->Reset(); }
	}

	for (auto& buffer : BuffersToDestroy) { device.destroyBuffer(buffer); }
	for (auto& image : ImagesToDestroy) { device.destroyImage(image); }
	for (auto& view : ImageViewsToDestroy) { device.destroyImageView(view); }
	for (auto& allocation : MemoryToFree) { vmaFreeMemory(Parent._allocator, allocation); }
	for (auto& semaphore : SemaphoresToDestroy) { device.destroySemaphore(semaphore); }
	for (auto& semaphore : SemaphoresToRecycle) { Parent.ReleaseSemaphore(semaphore); }
	BuffersToDestroy.clear();
	ImagesToDestroy.clear();
	ImageViewsToDestroy.clear();
	MemoryToFree.clear();
	SemaphoresToDestroy.clear();
	SemaphoresToRecycle.clear();
}
}  // namespace Vulkan
}  // namespace Luna
