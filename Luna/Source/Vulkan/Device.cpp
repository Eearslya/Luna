#include <Luna/Utility/Log.hpp>
#include <Luna/Utility/Threading.hpp>
#include <Luna/Vulkan/Buffer.hpp>
#include <Luna/Vulkan/CommandBuffer.hpp>
#include <Luna/Vulkan/CommandPool.hpp>
#include <Luna/Vulkan/Context.hpp>
#include <Luna/Vulkan/Device.hpp>
#include <Luna/Vulkan/Fence.hpp>
#include <Luna/Vulkan/Image.hpp>
#include <Luna/Vulkan/QueryPool.hpp>
#include <Luna/Vulkan/RenderPass.hpp>
#include <Luna/Vulkan/Semaphore.hpp>
#include <Luna/Vulkan/WSI.hpp>

#ifdef Luna_VULKAN_MT
static uint32_t GetThreadIndex() {
	return Threading::GetThreadID();
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
constexpr QueueType QueueFlushOrder[] = {QueueType::Transfer, QueueType::Graphics, QueueType::Compute};

Device::Device(Context& context)
		: _extensions(context._extensions),
			_instance(context._instance),
			_deviceInfo(context._deviceInfo),
			_queueInfo(context._queueInfo),
			_device(context._device) {
#ifdef LUNA_VULKAN_MT
	_nextCookie.store(0);
#endif

	// Create the VMA allocator.
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

		const VmaAllocatorCreateInfo allocatorCI = {.physicalDevice   = _deviceInfo.PhysicalDevice,
		                                            .device           = _device,
		                                            .pVulkanFunctions = &vmaFunctions,
		                                            .instance         = _instance,
		                                            .vulkanApiVersion = VK_API_VERSION_1_2};
		const auto allocatorResult               = vmaCreateAllocator(&allocatorCI, &_allocator);
		if (allocatorResult != VK_SUCCESS) {
			throw std::runtime_error("[Vulkan::Device] Failed to create memory allocator!");
		}
	}

	CreateTimelineSemaphores();

	CreateFrameContexts(2);

	_framebufferAllocator         = std::make_unique<FramebufferAllocator>(*this);
	_transientAttachmentAllocator = std::make_unique<TransientAttachmentAllocator>(*this);

	for (uint32_t i = 0; i < QueueTypeCount; ++i) {
		if (_queueInfo.Families[i] == VK_QUEUE_FAMILY_IGNORED) { continue; }

		bool aliased = false;
		for (int j = 0; j < i; j++) {
			if (_queueInfo.Families[i] == _queueInfo.Families[j]) {
				aliased = true;
				break;
			}
		}

		if (!aliased) {
			_queueData[i].QueryPool = MakeHandle<PerformanceQueryPool>(*this, _queueInfo.Families[i]);
			Log::Info("Vulkan-Performance", "Query Counters available for {}:", VulkanEnumToString(QueueType(i)));
			PerformanceQueryPool::LogCounters(_queueData[i].QueryPool->GetCounters(),
			                                  _queueData[i].QueryPool->GetDescriptions());
		}
	}
}

Device::~Device() noexcept {
	WaitIdle();

	_swapchainAcquire.Reset();
	_swapchainRelease.Reset();
	_swapchainImages.clear();

	_framebufferAllocator.reset();
	_transientAttachmentAllocator.reset();

	vmaDestroyAllocator(_allocator);

	for (auto& semaphore : _availableSemaphores) { _device.destroySemaphore(semaphore); }
	for (auto& fence : _availableFences) { _device.destroyFence(fence); }

	DestroyTimelineSemaphores();
}

vk::Format Device::GetDefaultDepthFormat() const {
	if (IsFormatSupported(
				vk::Format::eD32Sfloat, vk::FormatFeatureFlagBits::eDepthStencilAttachment, vk::ImageTiling::eOptimal)) {
		return vk::Format::eD32Sfloat;
	}
	if (IsFormatSupported(
				vk::Format::eX8D24UnormPack32, vk::FormatFeatureFlagBits::eDepthStencilAttachment, vk::ImageTiling::eOptimal)) {
		return vk::Format::eX8D24UnormPack32;
	}
	if (IsFormatSupported(
				vk::Format::eD16Unorm, vk::FormatFeatureFlagBits::eDepthStencilAttachment, vk::ImageTiling::eOptimal)) {
		return vk::Format::eD16Unorm;
	}

	return vk::Format::eUndefined;
}

vk::Format Device::GetDefaultDepthStencilFormat() const {
	if (IsFormatSupported(
				vk::Format::eD24UnormS8Uint, vk::FormatFeatureFlagBits::eDepthStencilAttachment, vk::ImageTiling::eOptimal)) {
		return vk::Format::eD24UnormS8Uint;
	}
	if (IsFormatSupported(
				vk::Format::eD32SfloatS8Uint, vk::FormatFeatureFlagBits::eDepthStencilAttachment, vk::ImageTiling::eOptimal)) {
		return vk::Format::eD32SfloatS8Uint;
	}

	return vk::Format::eUndefined;
}

vk::ImageViewType Device::GetImageViewType(const ImageCreateInfo& imageCI, const ImageViewCreateInfo* viewCI) const {
	const uint32_t baseLayer = viewCI ? viewCI->BaseLayer : 0;
	uint32_t layers          = viewCI ? viewCI->ArrayLayers : imageCI.ArrayLayers;
	if (layers == VK_REMAINING_ARRAY_LAYERS) { layers = imageCI.ArrayLayers - baseLayer; }

	const bool forceArray = viewCI ? (viewCI->MiscFlags & ImageViewCreateFlagBits::ForceArray)
	                               : (imageCI.MiscFlags & ImageCreateFlagBits::ForceArray);

	switch (imageCI.Type) {
		case vk::ImageType::e1D:
			if (layers > 1 || forceArray) {
				return vk::ImageViewType::e1DArray;
			} else {
				return vk::ImageViewType::e1D;
			}
			break;

		case vk::ImageType::e2D:
			if ((imageCI.MiscFlags & ImageCreateFlagBits::CubeCompatible) && (layers % 6) == 0) {
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

		default:
			throw std::runtime_error("Invalid image type given to GetImageViewType!");
	}
}

bool Device::IsFormatSupported(vk::Format format, vk::FormatFeatureFlags features, vk::ImageTiling tiling) const {
	const auto props = _deviceInfo.PhysicalDevice.getFormatProperties(format);
	const auto featureFlags =
		tiling == vk::ImageTiling::eOptimal ? props.optimalTilingFeatures : props.linearTilingFeatures;

	return (featureFlags & features) == features;
}

void Device::AddWaitSemaphore(CommandBufferType cbType,
                              SemaphoreHandle semaphore,
                              vk::PipelineStageFlags stages,
                              bool flush) {
	DeviceLock();
	AddWaitSemaphoreNoLock(GetQueueType(cbType), semaphore, stages, flush);
}

void Device::EndFrame() {
	DeviceFlush();
	EndFrameNoLock();
}

void Device::NextFrame() {
	DeviceFlush();

	EndFrameNoLock();

	_currentFrameContext = (_currentFrameContext + 1) % _frameContexts.size();
	Frame().Begin();
}

CommandBufferHandle Device::RequestCommandBuffer(CommandBufferType type) {
	return RequestCommandBufferForThread(GetThreadIndex(), type);
}

CommandBufferHandle Device::RequestCommandBufferForThread(uint32_t threadIndex, CommandBufferType type) {
	DeviceLock();
	return RequestCommandBufferNoLock(threadIndex, type, false);
}

CommandBufferHandle Device::RequestProfiledCommandBuffer(CommandBufferType type) {
	return RequestProfiledCommandBufferForThread(GetThreadIndex(), type);
}

CommandBufferHandle Device::RequestProfiledCommandBufferForThread(uint32_t threadIndex, CommandBufferType type) {
	DeviceLock();
	return RequestCommandBufferNoLock(threadIndex, type, true);
}

void Device::Submit(CommandBufferHandle& cmd, FenceHandle* fence, std::vector<SemaphoreHandle>* semaphores) {
	DeviceLock();
	SubmitNoLock(cmd, fence, semaphores);
}

void Device::WaitIdle() {
	DeviceFlush();
	WaitIdleNoLock();
}

BufferHandle Device::CreateBuffer(const BufferCreateInfo& bufferInfo, const void* initial) {
	const bool zeroInit = bufferInfo.Flags & BufferCreateFlagBits::ZeroInitialize;
	if (initial && zeroInit) {
		Log::Error("Vulkan", "Cannot create a buffer with initial data AND zero-initialize flag set!");
		return {};
	}

	BufferCreateInfo actualInfo = bufferInfo;
	actualInfo.Usage |= vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eTransferSrc;

	const vk::BufferCreateInfo bufferCI({}, actualInfo.Size, actualInfo.Usage, vk::SharingMode::eExclusive, nullptr);

	VmaAllocationCreateInfo bufferAI{.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE};
	if (actualInfo.Domain == BufferDomain::Host) {
		bufferAI.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
	}

	VkBuffer buffer;
	VmaAllocation allocation;
	VmaAllocationInfo allocationInfo;

	const VkResult res = vmaCreateBuffer(_allocator,
	                                     reinterpret_cast<const VkBufferCreateInfo*>(&bufferCI),
	                                     &bufferAI,
	                                     &buffer,
	                                     &allocation,
	                                     &allocationInfo);
	if (res != VK_SUCCESS) {
		Log::Error("Vulkan", "Failed to create buffer: {}", vk::to_string(vk::Result(res)));
		return {};
	}
	const auto& memType = _deviceInfo.Memory.memoryTypes[allocationInfo.memoryType];
	const bool hostVisible(memType.propertyFlags & vk::MemoryPropertyFlagBits::eHostVisible);

	void* bufferMap = nullptr;
	if (hostVisible) {
		if (vmaMapMemory(_allocator, allocation, &bufferMap) != VK_SUCCESS) {
			Log::Error("Vulkan", "Failed to map host-visible buffer!");
		}
	}

	BufferHandle handle(_bufferPool.Allocate(*this, buffer, allocation, actualInfo, bufferMap));

	if (initial || zeroInit) {
		if (bufferMap) {
			if (initial) {
				memcpy(bufferMap, initial, actualInfo.Size);
			} else {
				memset(bufferMap, 0, actualInfo.Size);
			}
		} else {
			CommandBufferHandle cmd;

			if (initial) {
				auto stagingInfo   = actualInfo;
				stagingInfo.Domain = BufferDomain::Host;
				auto stagingBuffer = CreateBuffer(stagingInfo, initial);

				cmd = RequestCommandBuffer(CommandBufferType::AsyncTransfer);
				cmd->CopyBuffer(*handle, *stagingBuffer);
			} else {
				cmd = RequestCommandBuffer(CommandBufferType::AsyncCompute);
				cmd->FillBuffer(*handle, 0);
			}

			DeviceLock();
			SubmitStaging(cmd, actualInfo.Usage, true);
		}
	}

	return handle;
}

ImageHandle Device::CreateImage(const ImageCreateInfo& imageCI, const ImageInitialData* initial) {
	if (initial) {
		auto stagingBuffer = CreateImageStagingBuffer(imageCI, initial);
		return CreateImageFromStagingBuffer(imageCI, &stagingBuffer);
	} else {
		return CreateImageFromStagingBuffer(imageCI, nullptr);
	}
}

ImageHandle Device::CreateImageFromStagingBuffer(const ImageCreateInfo& imageCI, const ImageInitialBuffer* buffer) {
	ImageManager manager(*this);

	vk::ImageCreateInfo createInfo(imageCI.Flags,
	                               imageCI.Type,
	                               imageCI.Format,
	                               vk::Extent3D(imageCI.Width, imageCI.Height, imageCI.Depth),
	                               imageCI.MipLevels,
	                               imageCI.ArrayLayers,
	                               imageCI.Samples,
	                               vk::ImageTiling::eOptimal,
	                               imageCI.Usage,
	                               vk::SharingMode::eExclusive,
	                               0,
	                               nullptr,
	                               vk::ImageLayout::eUndefined);

	if (imageCI.Domain == ImageDomain::Transient) { createInfo.usage |= vk::ImageUsageFlagBits::eTransientAttachment; }
	if (buffer) { createInfo.usage |= vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc; }
	if (createInfo.mipLevels == 0) { createInfo.mipLevels = CalculateMipLevels(createInfo.extent); }

	bool createUnormSrgbViews = false;
	std::vector<vk::Format> viewFormats;
	vk::ImageFormatListCreateInfo formatList;
	if (imageCI.MiscFlags & ImageCreateFlagBits::MutableSrgb) {
		viewFormats = ImageCreateInfo::GetComputeFormats(imageCI);
		if (!viewFormats.empty()) {
			createUnormSrgbViews = true;
			formatList.setViewFormats(viewFormats);
		}
	}

	if ((imageCI.Usage & vk::ImageUsageFlagBits::eStorage) || (imageCI.MiscFlags & ImageCreateFlagBits::MutableSrgb)) {
		createInfo.flags |= vk::ImageCreateFlagBits::eMutableFormat;
	}

	std::set<uint32_t> uniqueIndices;
	std::vector<uint32_t> sharingIndices;
	const auto queueFlags =
		imageCI.MiscFlags &
		(ImageCreateFlagBits::ConcurrentQueueGraphics | ImageCreateFlagBits::ConcurrentQueueAsyncCompute |
	   ImageCreateFlagBits::ConcurrentQueueAsyncGraphics | ImageCreateFlagBits::ConcurrentQueueAsyncTransfer);
	const bool concurrentQueue(queueFlags);
	if (concurrentQueue) {
		createInfo.sharingMode = vk::SharingMode::eConcurrent;

		if (buffer || queueFlags & (ImageCreateFlagBits::ConcurrentQueueGraphics |
		                            ImageCreateFlagBits::ConcurrentQueueAsyncGraphics)) {
			uniqueIndices.insert(_queueInfo.Family(QueueType::Graphics));
		}
		if (queueFlags & ImageCreateFlagBits::ConcurrentQueueAsyncCompute) {
			uniqueIndices.insert(_queueInfo.Family(QueueType::Compute));
		}
		if (buffer || (queueFlags & ImageCreateFlagBits::ConcurrentQueueAsyncTransfer)) {
			uniqueIndices.insert(_queueInfo.Family(QueueType::Transfer));
		}

		sharingIndices = std::vector<uint32_t>(uniqueIndices.begin(), uniqueIndices.end());
		if (sharingIndices.size() > 1) {
			createInfo.setQueueFamilyIndices(sharingIndices);
		} else {
			createInfo.setQueueFamilyIndices(nullptr);
		}
	}

	{
		vk::StructureChain chain(createInfo, formatList);
		if (!createUnormSrgbViews) { chain.unlink<vk::ImageFormatListCreateInfo>(); }
		const VkImageCreateInfo imageCreateInfo(chain.get<vk::ImageCreateInfo>());
		const VmaAllocationCreateInfo imageAllocateInfo{.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE};
		VkImage image = VK_NULL_HANDLE;
		VmaAllocation allocation;
		const VkResult result =
			vmaCreateImage(_allocator, &imageCreateInfo, &imageAllocateInfo, &image, &allocation, nullptr);
		if (result != VK_SUCCESS) {
			Log::Error("Vulkan", "Failed to create image: {}", vk::to_string(vk::Result(result)));
			throw std::runtime_error("Failed to create image!");
		}
		manager.Image      = image;
		manager.Allocation = allocation;
	}

	auto tmpCI      = imageCI;
	tmpCI.Usage     = createInfo.usage;
	tmpCI.Flags     = createInfo.flags;
	tmpCI.MipLevels = createInfo.mipLevels;

	const bool hasView(createInfo.usage &
	                   (vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage |
	                    vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eDepthStencilAttachment |
	                    vk::ImageUsageFlagBits::eInputAttachment));
	if (hasView) {
		if (!manager.CreateDefaultViews(tmpCI, nullptr, createUnormSrgbViews, viewFormats.data())) { return {}; }
	}

	ImageHandle handle(
		_imagePool.Allocate(*this, manager.Image, manager.ImageView, manager.Allocation, tmpCI, manager.DefaultViewType));
	if (handle) {
		manager.Owned = false;
		if (hasView) {
			handle->GetView().SetAltViews(manager.DepthView, manager.StencilView);
			handle->GetView().SetRenderTargetViews(std::move(manager.RenderTargetViews));
			handle->GetView().SetUnormView(manager.UnormView);
			handle->GetView().SetSrgbView(manager.SrgbView);
		}

		handle->SetStages(ImageUsageToStages(createInfo.usage));
		handle->SetAccess(ImageUsageToAccess(createInfo.usage));
	}

	const bool shareCompute = (queueFlags & ImageCreateFlagBits::ConcurrentQueueAsyncCompute) &&
	                          !_queueInfo.SameQueue(QueueType::Graphics, QueueType::Compute);
	const bool shareAsyncGraphics = GetQueueType(CommandBufferType::AsyncGraphics) == QueueType::Compute &&
	                                (queueFlags & ImageCreateFlagBits::ConcurrentQueueAsyncGraphics);

	CommandBufferHandle transitionCmd;
	if (buffer) {
		const bool generateMips = imageCI.MiscFlags & ImageCreateFlagBits::GenerateMipmaps;

		vk::AccessFlags finalTransitionSrcAccess = {};
		if (generateMips) {
			finalTransitionSrcAccess = vk::AccessFlagBits::eTransferRead;
		} else if (_queueInfo.SameQueue(QueueType::Graphics, QueueType::Transfer)) {
			finalTransitionSrcAccess = vk::AccessFlagBits::eTransferWrite;
		}

		vk::AccessFlags prepareSrcAccess = _queueInfo.SameQueue(QueueType::Graphics, QueueType::Transfer)
		                                     ? vk::AccessFlagBits::eTransferWrite
		                                     : vk::AccessFlagBits{};
		bool needMipmapBarrier           = true;
		bool needInitialBarrier          = true;

		auto graphicsCmd = RequestCommandBuffer(CommandBufferType::Generic);
		CommandBufferHandle transferCmd;
		if (!_queueInfo.SameQueue(QueueType::Transfer, QueueType::Graphics)) {
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
		transferCmd->CopyBufferToImage(*handle, *buffer->Buffer, buffer->Blits);

		if (!_queueInfo.SameQueue(QueueType::Graphics, QueueType::Transfer)) {
			const vk::PipelineStageFlags dstStages =
				generateMips ? vk::PipelineStageFlagBits::eTransfer : handle->GetStages();

			if (!concurrentQueue && !_queueInfo.SameFamily(QueueType::Graphics, QueueType::Transfer)) {
				needMipmapBarrier = false;
				if (!generateMips) { needInitialBarrier = false; }

				const vk::ImageMemoryBarrier release(
					vk::AccessFlagBits::eTransferWrite,
					{},
					vk::ImageLayout::eTransferDstOptimal,
					generateMips ? vk::ImageLayout::eTransferSrcOptimal : imageCI.InitialLayout,
					_queueInfo.Family(QueueType::Transfer),
					_queueInfo.Family(QueueType::Graphics),
					handle->GetImage(),
					vk::ImageSubresourceRange(
						FormatAspectFlags(imageCI.Format), 0, generateMips ? 1 : createInfo.mipLevels, 0, createInfo.arrayLayers));

				const vk::ImageMemoryBarrier acquire({},
				                                     generateMips
				                                       ? vk::AccessFlagBits::eTransferRead
				                                       : handle->GetAccess() & ImageLayoutToAccess(imageCI.InitialLayout),
				                                     release.oldLayout,
				                                     release.newLayout,
				                                     release.srcQueueFamilyIndex,
				                                     release.dstQueueFamilyIndex,
				                                     release.image,
				                                     release.subresourceRange);

				transferCmd->Barrier(
					vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eBottomOfPipe, {}, {}, {release});
				transferCmd->Barrier(dstStages, dstStages, {}, {}, {acquire});
			}

			std::vector<SemaphoreHandle> semaphores(1);
			Submit(transferCmd, nullptr, &semaphores);
			AddWaitSemaphore(CommandBufferType::Generic, semaphores[0], dstStages, true);
		}

		if (generateMips) {
			graphicsCmd->BarrierPrepareGenerateMipmaps(*handle,
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
				handle->GetStages(),
				handle->GetAccess() & ImageLayoutToAccess(imageCI.InitialLayout));
		}

		transitionCmd = std::move(graphicsCmd);
	} else if (imageCI.InitialLayout != vk::ImageLayout::eUndefined) {
		transitionCmd = RequestCommandBuffer(CommandBufferType::Generic);
		transitionCmd->ImageBarrier(*handle,
		                            createInfo.initialLayout,
		                            imageCI.InitialLayout,
		                            vk::PipelineStageFlagBits::eTopOfPipe,
		                            {},
		                            handle->GetStages(),
		                            handle->GetAccess() & ImageLayoutToAccess(imageCI.InitialLayout));
	}

	if (transitionCmd) {
		if (shareCompute || shareAsyncGraphics) {
			std::vector<SemaphoreHandle> semaphores(1);
			Submit(transitionCmd, nullptr, &semaphores);

			vk::PipelineStageFlags dstStages = handle->GetStages();
			if (!_queueInfo.SameFamily(QueueType::Graphics, QueueType::Compute)) {
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

ImageInitialBuffer Device::CreateImageStagingBuffer(const ImageCreateInfo& imageCI, const ImageInitialData* initial) {
	ImageInitialBuffer result;

	bool generateMips = imageCI.MiscFlags & ImageCreateFlagBits::GenerateMipmaps;
	TextureFormatLayout layout;

	uint32_t copyLevels = imageCI.MipLevels;
	if (generateMips) {
		copyLevels = 1;
	} else if (imageCI.MipLevels == 0) {
		copyLevels = CalculateMipLevels(imageCI.Width, imageCI.Height, imageCI.Depth);
	}

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

	const BufferCreateInfo bufferCI{BufferDomain::Host, layout.GetRequiredSize(), vk::BufferUsageFlagBits::eTransferSrc};
	result.Buffer = CreateBuffer(bufferCI);

	uint32_t index = 0;
	uint8_t* data  = reinterpret_cast<uint8_t*>(result.Buffer->Map());
	layout.SetBuffer(layout.GetRequiredSize(), data);

	for (uint32_t level = 0; level < copyLevels; ++level) {
		const auto& mipInfo            = layout.GetMipInfo(level);
		const uint32_t dstHeightStride = layout.GetLayerSize(level);
		const size_t rowSize           = layout.GetRowSize(level);

		for (uint32_t layer = 0; layer < imageCI.ArrayLayers; ++layer, ++index) {
			const uint32_t srcRowLength    = initial[index].RowLength ? initial[index].RowLength : mipInfo.RowLength;
			const uint32_t srcArrayHeight  = initial[index].ImageHeight ? initial[index].ImageHeight : mipInfo.ImageHeight;
			const uint32_t srcRowStride    = layout.RowByteStride(srcRowLength);
			const uint32_t srcHeightStride = layout.LayerByteStride(srcArrayHeight, srcRowStride);

			uint8_t* dst       = reinterpret_cast<uint8_t*>(layout.Data(layer, level));
			const uint8_t* src = reinterpret_cast<const uint8_t*>(initial[index].Data);
			for (uint32_t z = 0; z < mipInfo.Depth; ++z) {
				for (uint32_t y = 0; y < mipInfo.BlockImageHeight; ++y) {
					memcpy(dst + z * dstHeightStride + y * rowSize, src + z * srcHeightStride + y * srcRowStride, rowSize);
				}
			}
		}
	}

	result.Blits = layout.BuildBufferImageCopies();

	return result;
}

ImageInitialBuffer Device::CreateImageStagingBuffer(const TextureFormatLayout& layout) {
	ImageInitialBuffer result;

	const BufferCreateInfo bufferCI{BufferDomain::Host, layout.GetRequiredSize(), vk::BufferUsageFlagBits::eTransferSrc};
	result.Buffer = CreateBuffer(bufferCI, layout.Data());
	result.Blits  = layout.BuildBufferImageCopies();

	return result;
}

ImageViewHandle Device::CreateImageView(const ImageViewCreateInfo& viewCI) {
	const auto& imageCI = viewCI.Image->GetCreateInfo();
	const vk::ImageViewCreateInfo viewInfo(
		{},
		viewCI.Image->GetImage(),
		viewCI.ViewType,
		viewCI.Format,
		viewCI.Swizzle,
		vk::ImageSubresourceRange(
			FormatAspectFlags(viewCI.Format), viewCI.BaseLevel, viewCI.MipLevels, viewCI.BaseLayer, viewCI.ArrayLayers));

	auto imageView = _device.createImageView(viewInfo);
	Log::Debug("Vulkan", "Image View created.");

	return ImageViewHandle(_imageViewPool.Allocate(*this, imageView, viewCI));
}

ImageView& Device::GetSwapchainView() {
	return GetSwapchainView(_swapchainIndex);
}

ImageView& Device::GetSwapchainView(uint32_t index) {
	return _swapchainImages[index]->GetView();
}

RenderPassInfo Device::GetSwapchainRenderPass(SwapchainRenderPassType type) {
	RenderPassInfo info       = {};
	info.ColorAttachmentCount = 1;
	info.ColorAttachments[0]  = &GetSwapchainView();
	info.ClearAttachmentMask  = 1 << 0;
	info.StoreAttachmentMask  = 1 << 0;

	const auto& imageCI = info.ColorAttachments[0]->GetImage()->GetCreateInfo();
	const vk::Extent2D extent(imageCI.Width, imageCI.Height);

	switch (type) {
		case SwapchainRenderPassType::Depth: {
			info.Flags |= RenderPassOpFlagBits::ClearDepthStencil;
			auto depth                  = GetTransientAttachment(extent, GetDefaultDepthFormat());
			info.DepthStencilAttachment = &depth->GetView();
		} break;

		case SwapchainRenderPassType::DepthStencil: {
			info.Flags |= RenderPassOpFlagBits::ClearDepthStencil;
			auto depthStencil           = GetTransientAttachment(extent, GetDefaultDepthStencilFormat());
			info.DepthStencilAttachment = &depthStencil->GetView();
		} break;

		default:
			break;
	}

	return info;
}

ImageHandle Device::GetTransientAttachment(const vk::Extent2D& extent,
                                           vk::Format format,
                                           uint32_t index,
                                           vk::SampleCountFlagBits samples,
                                           uint32_t arrayLayers) {
	return _transientAttachmentAllocator->RequestAttachment(extent, format, index, samples, arrayLayers);
}

SemaphoreHandle Device::RequestSemaphore() {
	DeviceLock();
	auto semaphore = AllocateSemaphore();
	return SemaphoreHandle(_semaphorePool.Allocate(*this, semaphore, false, true));
}

void Device::AddWaitSemaphoreNoLock(QueueType queueType,
                                    SemaphoreHandle semaphore,
                                    vk::PipelineStageFlags stages,
                                    bool flush) {
	if (flush) { FlushFrame(queueType); }

	auto& data = _queueData[int(queueType)];
	semaphore->SetPendingWait();
	data.WaitSemaphores.push_back(semaphore);
	data.WaitStages.push_back(stages);
	data.NeedsFence = true;
}

uint64_t Device::AllocateCookie() {
#ifdef LUNA_VULKAN_MT
	return _nextCookie.fetch_add(16, std::memory_order_relaxed) + 16;
#else
	_nextCookie += 16;
	return _nextCookie;
#endif
}

vk::Fence Device::AllocateFence() {
	if (_availableFences.empty()) {
		const vk::FenceCreateInfo fenceCI;
		auto fence = _device.createFence(fenceCI);
		Log::Debug("Vulkan", "Fence created.");
		_availableFences.push_back(fence);
	}

	auto fence = _availableFences.back();
	_availableFences.pop_back();

	return fence;
}

vk::Semaphore Device::AllocateSemaphore() {
	if (_availableSemaphores.empty()) {
		const vk::SemaphoreCreateInfo semaphoreCI;
		auto semaphore = _device.createSemaphore(semaphoreCI);
		Log::Debug("Vulkan", "Semaphore created.");
		_availableSemaphores.push_back(semaphore);
	}

	auto semaphore = _availableSemaphores.back();
	_availableSemaphores.pop_back();

	return semaphore;
}

SemaphoreHandle Device::ConsumeReleaseSemaphore() {
	return std::move(_swapchainRelease);
}

void Device::CreateFrameContexts(uint32_t count) {
	DeviceFlush();
	WaitIdleNoLock();

	_frameContexts.clear();
	for (uint32_t i = 0; i < count; ++i) { _frameContexts.push_back(std::make_unique<FrameContext>(*this, i)); }
}

void Device::CreateTimelineSemaphores() {
	if (!_deviceInfo.EnabledFeatures.Vulkan12.timelineSemaphore) { return; }

	const vk::SemaphoreCreateInfo semaphoreCI;
	const vk::SemaphoreTypeCreateInfo semaphoreType(vk::SemaphoreType::eTimeline, 0);
	const vk::StructureChain chain(semaphoreCI, semaphoreType);
	for (auto& queue : _queueData) {
		queue.TimelineSemaphore = _device.createSemaphore(chain.get());
		Log::Debug("Vulkan", "Timeline semaphore created.");
	}
}

void Device::DestroyTimelineSemaphores() {
	if (!_deviceInfo.EnabledFeatures.Vulkan12.timelineSemaphore) { return; }

	for (auto& queue : _queueData) {
		if (queue.TimelineSemaphore) {
			_device.destroySemaphore(queue.TimelineSemaphore);
			queue.TimelineSemaphore = nullptr;
		}
	}
}

QueueType Device::GetQueueType(CommandBufferType cmdType) const {
	if (cmdType != CommandBufferType::AsyncGraphics) {
		return static_cast<QueueType>(cmdType);
	} else {
		if (_queueInfo.SameFamily(QueueType::Graphics, QueueType::Compute) &&
		    !_queueInfo.SameQueue(QueueType::Graphics, QueueType::Compute)) {
			return QueueType::Compute;
		} else {
			return QueueType::Graphics;
		}
	}
}

Device::FrameContext& Device::Frame() {
	return *_frameContexts[_currentFrameContext];
}

void Device::ReleaseFence(vk::Fence fence) {
	_availableFences.push_back(fence);
}

void Device::ReleaseSemaphore(vk::Semaphore semaphore) {
	_availableSemaphores.push_back(semaphore);
}

const Framebuffer& Device::RequestFramebuffer(const RenderPassInfo& rpInfo) {
	return _framebufferAllocator->RequestFramebuffer(rpInfo);
}

const RenderPass& Device::RequestRenderPass(const RenderPassInfo& rpInfo, bool compatible) {
	Hasher h;

	std::array<vk::Format, MaxColorAttachments> colorFormats;
	vk::Format depthFormat = vk::Format::eUndefined;
	uint32_t lazy          = 0;
	uint32_t optimal       = 0;

	for (uint32_t i = 0; i < rpInfo.ColorAttachmentCount; ++i) {
		colorFormats[i] = rpInfo.ColorAttachments[i]->GetFormat();
		if (rpInfo.ColorAttachments[i]->GetImage()->GetCreateInfo().Domain == ImageDomain::Transient) { lazy |= 1u << i; }
		if (rpInfo.ColorAttachments[i]->GetImage()->GetLayoutType() == ImageLayout::Optimal) { optimal |= 1u << i; }
		h(rpInfo.ColorAttachments[i]->GetImage()->GetSwapchainLayout());
	}

	if (rpInfo.DepthStencilAttachment) {
		if (rpInfo.DepthStencilAttachment->GetImage()->GetCreateInfo().Domain == ImageDomain::Transient) {
			lazy |= 1u << rpInfo.ColorAttachmentCount;
		}
		if (rpInfo.DepthStencilAttachment->GetImage()->GetLayoutType() == ImageLayout::Optimal) {
			optimal |= 1u << rpInfo.ColorAttachmentCount;
		}
	}

	h(rpInfo.BaseLayer);
	h(rpInfo.ArrayLayers);
	h(rpInfo.Subpasses.size());
	for (const auto& subpass : rpInfo.Subpasses) {
		h(subpass.ColorAttachmentCount);
		h(subpass.InputAttachmentCount);
		h(subpass.ResolveAttachmentCount);
		h(subpass.DepthStencil);
		for (uint32_t i = 0; i < subpass.ColorAttachmentCount; ++i) { h(subpass.ColorAttachments[i]); }
		for (uint32_t i = 0; i < subpass.InputAttachmentCount; ++i) { h(subpass.InputAttachments[i]); }
		for (uint32_t i = 0; i < subpass.ResolveAttachmentCount; ++i) { h(subpass.ResolveAttachments[i]); }
	}

	h(rpInfo.ColorAttachmentCount);
	for (const auto format : colorFormats) { h(format); }
	h(rpInfo.DepthStencilAttachment ? rpInfo.DepthStencilAttachment->GetFormat() : vk::Format::eUndefined);

	if (!compatible) {
		h(uint32_t(rpInfo.Flags));
		h(rpInfo.ClearAttachmentMask);
		h(rpInfo.LoadAttachmentMask);
		h(rpInfo.StoreAttachmentMask);
		h(optimal);
	}

	h(lazy);

	const auto hash = h.Get();
	auto* ret       = _renderPasses.Find(hash);
	if (!ret) { ret = _renderPasses.EmplaceYield(hash, hash, *this, rpInfo); }

	return *ret;
}

void Device::SetAcquireSemaphore(uint32_t imageIndex, SemaphoreHandle& semaphore) {
	_swapchainAcquire         = std::move(semaphore);
	_swapchainAcquireConsumed = false;
	_swapchainIndex           = imageIndex;

	if (_swapchainAcquire) { _swapchainAcquire->SetInternalSync(); }
}

void Device::SetupSwapchain(WSI& wsi) {
	DeviceFlush();
	WaitIdleNoLock();

	const auto& extent = wsi._swapchainExtent;
	const auto& format = wsi._swapchainFormat.format;
	const auto& images = wsi._swapchainImages;
	const auto imageCI = ImageCreateInfo::RenderTarget(format, extent.width, extent.height);

	_swapchainAcquireConsumed = false;
	_swapchainImages.clear();
	_swapchainImages.reserve(images.size());
	_swapchainIndex = std::numeric_limits<uint32_t>::max();

	for (size_t i = 0; i < images.size(); ++i) {
		const auto& image = images[i];

		const vk::ImageViewCreateInfo viewCI({},
		                                     image,
		                                     vk::ImageViewType::e2D,
		                                     format,
		                                     vk::ComponentMapping(),
		                                     vk::ImageSubresourceRange(FormatAspectFlags(format), 0, 1, 0, 1));
		auto imageView = _device.createImageView(viewCI);
		Log::Debug("Vulkan", "Image View created.");

		Image* img = _imagePool.Allocate(*this, image, imageView, VmaAllocation{}, imageCI, viewCI.viewType);
		ImageHandle handle(img);
		handle->DisownImage();
		handle->DisownMemory();
		handle->SetInternalSync();
		handle->GetView().SetInternalSync();
		handle->SetSwapchainLayout(vk::ImageLayout::ePresentSrcKHR);

		_swapchainImages.push_back(handle);
	}
}

void Device::EndFrameNoLock() {
	InternalFence fence;
	for (const auto q : QueueFlushOrder) {
		if (_queueData[int(q)].NeedsFence || !Frame().Submissions[int(q)].empty()) {
			SubmitQueue(q, &fence, nullptr);
			if (fence.Fence) {
				Frame().FencesToAwait.push_back(fence.Fence);
				Frame().FencesToRecycle.push_back(fence.Fence);
			}
			_queueData[int(q)].NeedsFence = false;
		}
	}
}

void Device::FlushFrame(QueueType queueType) {
	if (!_queueInfo.Queue(queueType)) { return; }
	SubmitQueue(queueType, nullptr, nullptr);
}

CommandBufferHandle Device::RequestCommandBufferNoLock(uint32_t threadIndex, CommandBufferType type, bool profiled) {
	const auto queueType = GetQueueType(type);
	auto& cmdPool        = Frame().CommandPools[int(queueType)][threadIndex];
	auto cmdBuf          = cmdPool->RequestCommandBuffer();

	if (profiled && !_deviceInfo.EnabledFeatures.PerformanceQuery.performanceCounterQueryPools) {
		Log::Warning("Vulkan", "Profiled command buffer was requested, but the current device does not support it.");
		profiled = false;
	}

	const vk::CommandBufferBeginInfo cmdBI(vk::CommandBufferUsageFlagBits::eOneTimeSubmit, nullptr);
	cmdBuf.begin(cmdBI);
	_lock.Counter++;
	CommandBufferHandle handle(_commandBufferPool.Allocate(*this, cmdBuf, type, threadIndex));

	if (profiled) {
		// TODO
	}

	return handle;
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
			*fence = FenceHandle(_fencePool.Allocate(*this, internalFence.Timeline, internalFence.TimelineValue));
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
	auto& queueData          = _queueData[int(queueType)];
	auto& submissions        = Frame().Submissions[int(queueType)];
	const bool hasSemaphores = semaphores != nullptr && semaphores->size() != 0;

	if (submissions.empty() && submitFence == nullptr && !hasSemaphores) { return; }
	if (queueType != QueueType::Transfer) { FlushFrame(QueueType::Transfer); }

	vk::Queue queue                        = _queueInfo.Queue(queueType);
	vk::Semaphore timelineSemaphore        = queueData.TimelineSemaphore;
	uint64_t timelineValue                 = ++queueData.TimelineValue;
	Frame().TimelineValues[int(queueType)] = timelineValue;

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

			if (!batches[batch].SignalSemaphores.empty()) { ++batch; }

			batches[batch].CommandBuffers.push_back(cmdBufHandle->GetCommandBuffer());

			vk::Semaphore release = AllocateSemaphore();
			_swapchainRelease     = SemaphoreHandle(_semaphorePool.Allocate(*this, release, true, true));
			_swapchainRelease->SetInternalSync();
			batches[batch].SignalSemaphores.push_back(release);
			batches[batch].SignalValues.push_back(0);
		} else {
			if (!batches[batch].SignalSemaphores.empty()) { ++batch; }

			batches[batch].CommandBuffers.push_back(cmdBufHandle->GetCommandBuffer());
		}
	}
	submissions.clear();

	// Only use a fence if we have to. Prefer using the timeline semaphore.
	vk::Fence fence = nullptr;
	if (submitFence && !_deviceInfo.EnabledFeatures.Vulkan12.timelineSemaphore) {
		fence              = AllocateFence();
		submitFence->Fence = fence;
	}

	// Emit any necessary semaphores from the final batch.
	if (_deviceInfo.EnabledFeatures.Vulkan12.timelineSemaphore) {
		batches[batch].SignalSemaphores.push_back(timelineSemaphore);
		batches[batch].SignalValues.push_back(timelineValue);
		batches[batch].HasTimeline = true;

		if (submitFence) {
			submitFence->Fence         = nullptr;
			submitFence->Timeline      = timelineSemaphore;
			submitFence->TimelineValue = timelineValue;
		}

		if (hasSemaphores) {
			for (size_t i = 0; i < semaphores->size(); ++i) {
				(*semaphores)[i] = SemaphoreHandle(_semaphorePool.Allocate(*this, timelineSemaphore, timelineValue, false));
			}
		}
	} else {
		if (submitFence) {
			submitFence->Timeline      = nullptr;
			submitFence->TimelineValue = 0;
		}

		if (hasSemaphores) {
			for (size_t i = 0; i < semaphores->size(); ++i) {
				vk::Semaphore sem = AllocateSemaphore();
				batches[batch].SignalSemaphores.push_back(sem);
				batches[batch].SignalValues.push_back(0);
				(*semaphores)[i] = SemaphoreHandle(_semaphorePool.Allocate(*this, sem, true, true));
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
		Log::Error("Vulkan", "Error occurred on command submission: {}", vk::to_string(submitResult));
	}

	// If we weren't able to use a timeline semaphore, we need to make sure there is a fence in place to wait for
	// completion.
	if (!_deviceInfo.EnabledFeatures.Vulkan12.timelineSemaphore) { queueData.NeedsFence = true; }
}

void Device::SubmitStaging(CommandBufferHandle& cmd, vk::BufferUsageFlags usage, bool flush) {
	const auto access  = BufferUsageToAccess(usage);
	const auto stages  = BufferUsageToStages(usage);
	vk::Queue srcQueue = _queueInfo.Queue(GetQueueType(cmd->GetType()));

	if (srcQueue == _queueInfo.Queue(QueueType::Graphics) && srcQueue == _queueInfo.Queue(QueueType::Compute)) {
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

		if (srcQueue == _queueInfo.Queue(QueueType::Graphics)) {
			cmd->Barrier(vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferWrite, graphicsStages, access);

			if (computeStages) {
				std::vector<SemaphoreHandle> semaphores(1);
				SubmitNoLock(cmd, nullptr, &semaphores);
				AddWaitSemaphoreNoLock(QueueType::Compute, semaphores[0], computeStages, flush);
			} else {
				SubmitNoLock(cmd, nullptr, nullptr);
			}
		} else if (srcQueue == _queueInfo.Queue(QueueType::Compute)) {
			cmd->Barrier(
				vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferWrite, computeStages, computeAccess);

			if (graphicsStages) {
				std::vector<SemaphoreHandle> semaphores(1);
				SubmitNoLock(cmd, nullptr, &semaphores);
				AddWaitSemaphoreNoLock(QueueType::Graphics, semaphores[0], graphicsStages, flush);
			} else {
				SubmitNoLock(cmd, nullptr, nullptr);
			}
		} else {
			if (graphicsStages && computeStages) {
				std::vector<SemaphoreHandle> semaphores(2);
				SubmitNoLock(cmd, nullptr, &semaphores);
				AddWaitSemaphoreNoLock(QueueType::Graphics, semaphores[0], graphicsStages, flush);
				AddWaitSemaphoreNoLock(QueueType::Compute, semaphores[1], computeStages, flush);
			} else if (graphicsStages) {
				std::vector<SemaphoreHandle> semaphores(1);
				SubmitNoLock(cmd, nullptr, &semaphores);
				AddWaitSemaphoreNoLock(QueueType::Graphics, semaphores[0], graphicsStages, flush);
			} else if (computeStages) {
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

	if (_framebufferAllocator) { _framebufferAllocator->Clear(); }
	if (_transientAttachmentAllocator) { _transientAttachmentAllocator->Clear(); }

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

void Device::DestroyBuffer(vk::Buffer buffer) {
	DeviceLock();
	DestroyBufferNoLock(buffer);
}

void Device::DestroyBufferNoLock(vk::Buffer buffer) {
	Frame().BuffersToDestroy.push_back(buffer);
}

void Device::DestroyFramebuffer(vk::Framebuffer framebuffer) {
	DeviceLock();
	DestroyFramebufferNoLock(framebuffer);
}

void Device::DestroyFramebufferNoLock(vk::Framebuffer framebuffer) {
	Frame().FramebuffersToDestroy.push_back(framebuffer);
}

void Device::DestroyImage(vk::Image image) {
	DeviceLock();
	DestroyImageNoLock(image);
}

void Device::DestroyImageNoLock(vk::Image image) {
	Frame().ImagesToDestroy.push_back(image);
}

void Device::DestroyImageView(vk::ImageView view) {
	DeviceLock();
	DestroyImageViewNoLock(view);
}

void Device::DestroyImageViewNoLock(vk::ImageView view) {
	Frame().ImageViewsToDestroy.push_back(view);
}

void Device::DestroySemaphore(vk::Semaphore semaphore) {
	DeviceLock();
	DestroySemaphoreNoLock(semaphore);
}

void Device::DestroySemaphoreNoLock(vk::Semaphore semaphore) {
	Frame().SemaphoresToDestroy.push_back(semaphore);
}

void Device::FreeAllocation(const VmaAllocation& allocation) {
	DeviceLock();
	FreeAllocationNoLock(allocation);
}

void Device::FreeAllocationNoLock(const VmaAllocation& allocation) {
	Frame().AllocationsToFree.push_back(allocation);
}

void Device::RecycleSemaphore(vk::Semaphore semaphore) {
	DeviceLock();
	RecycleSemaphoreNoLock(semaphore);
}

void Device::RecycleSemaphoreNoLock(vk::Semaphore semaphore) {
	Frame().SemaphoresToRecycle.push_back(semaphore);
}

void Device::ResetFence(vk::Fence fence, bool observedWait) {
	DeviceLock();
	ResetFenceNoLock(fence, observedWait);
}

void Device::ResetFenceNoLock(vk::Fence fence, bool observedWait) {
	if (observedWait) {
		_device.resetFences(fence);
		ReleaseFence(fence);
	} else {
		Frame().FencesToRecycle.push_back(fence);
	}
}

Device::FrameContext::FrameContext(Device& device, uint32_t frameIndex) : Parent(device), FrameIndex(frameIndex) {
	std::fill(TimelineValues.begin(), TimelineValues.end(), 0);

	const uint32_t threadCount = Threading::Get()->GetThreadCount();
	for (uint32_t q = 0; q < QueueTypeCount; ++q) {
		CommandPools[q].reserve(threadCount);
		TimelineValues[q] = Parent._queueData[q].TimelineValue;
		for (uint32_t i = 0; i < threadCount; ++i) {
			CommandPools[q].push_back(std::make_unique<CommandPool>(Parent, Parent._queueInfo.Families[q]));
		}
	}
}

Device::FrameContext::~FrameContext() noexcept {
	Begin();
}

void Device::FrameContext::Begin() {
	auto device = Parent._device;

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
				const auto waitResult = device.waitSemaphores(waitInfo, std::numeric_limits<uint64_t>::max());
				if (waitResult != vk::Result::eSuccess) { Log::Error("Vulkan", "Failed to wait on Timeline Semaphores!"); }
			}
		}
	}

	if (!FencesToAwait.empty()) {
		const auto waitResult = device.waitForFences(FencesToAwait, VK_TRUE, std::numeric_limits<uint64_t>::max());
		if (waitResult != vk::Result::eSuccess) { Log::Error("Vulkan", "Failed to wait on Fences!"); }
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
	for (auto& framebuffer : FramebuffersToDestroy) { device.destroyFramebuffer(framebuffer); }
	for (auto& image : ImagesToDestroy) { device.destroyImage(image); }
	for (auto& view : ImageViewsToDestroy) { device.destroyImageView(view); }
	for (auto& allocation : AllocationsToFree) { vmaFreeMemory(Parent._allocator, allocation); }
	for (auto& semaphore : SemaphoresToDestroy) { device.destroySemaphore(semaphore); }
	for (auto& semaphore : SemaphoresToRecycle) { Parent.ReleaseSemaphore(semaphore); }
	BuffersToDestroy.clear();
	FramebuffersToDestroy.clear();
	ImagesToDestroy.clear();
	ImageViewsToDestroy.clear();
	AllocationsToFree.clear();
	SemaphoresToDestroy.clear();
	SemaphoresToRecycle.clear();
}

Device::ImageManager::ImageManager(Device& device) : Parent(device) {}

Device::ImageManager::~ImageManager() noexcept {
	vk::Device device = Parent.GetDevice();

	if (Owned) {
		if (ImageView) { device.destroyImageView(ImageView); }
		if (DepthView) { device.destroyImageView(DepthView); }
		if (StencilView) { device.destroyImageView(StencilView); }
		if (UnormView) { device.destroyImageView(UnormView); }
		if (SrgbView) { device.destroyImageView(SrgbView); }
		for (auto& view : RenderTargetViews) { device.destroyImageView(view); }

		if (Image) { device.destroyImage(Image); }
		if (Allocation) { vmaFreeMemory(Parent._allocator, Allocation); }
	}
}

bool Device::ImageManager::CreateDefaultViews(const ImageCreateInfo& imageCI,
                                              const vk::ImageViewCreateInfo* viewInfo,
                                              bool createUnormSrgbViews,
                                              const vk::Format* viewFormats) {
	vk::Device device = Parent.GetDevice();

	vk::ImageViewCreateInfo viewCI(
		{},
		Image,
		Parent.GetImageViewType(imageCI, nullptr),
		imageCI.Format,
		vk::ComponentMapping(),
		vk::ImageSubresourceRange(FormatAspectFlags(imageCI.Format), 0, imageCI.MipLevels, 0, imageCI.ArrayLayers));
	if (viewInfo) { viewCI = *viewInfo; }

	if (!CreateAltViews(imageCI, viewCI)) { return false; }
	if (!CreateRenderTargetViews(imageCI, viewCI)) { return false; }
	if (!CreateDefaultView(viewCI)) { return false; }
	if (createUnormSrgbViews) {
		auto tmpCI = viewCI;

		tmpCI.format = viewFormats[0];
		UnormView    = device.createImageView(tmpCI);

		tmpCI.format = viewFormats[1];
		SrgbView     = device.createImageView(tmpCI);
	}

	return true;
}

bool Device::ImageManager::CreateAltViews(const ImageCreateInfo& imageCI, const vk::ImageViewCreateInfo& viewCI) {
	if (viewCI.viewType == vk::ImageViewType::eCube || viewCI.viewType == vk::ImageViewType::eCubeArray ||
	    viewCI.viewType == vk::ImageViewType::e3D) {
		return true;
	}

	if (viewCI.subresourceRange.aspectMask == (vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil)) {
		if ((imageCI.Usage & ~vk::ImageUsageFlagBits::eDepthStencilAttachment)) {
			auto viewInfo = viewCI;

			viewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth;
			DepthView                            = Parent.GetDevice().createImageView(viewInfo);

			viewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eStencil;
			StencilView                          = Parent.GetDevice().createImageView(viewInfo);
		}
	}

	return true;
}

bool Device::ImageManager::CreateDefaultView(const vk::ImageViewCreateInfo& viewCI) {
	ImageView = Parent.GetDevice().createImageView(viewCI);

	return true;
}

bool Device::ImageManager::CreateRenderTargetViews(const ImageCreateInfo& imageCI,
                                                   const vk::ImageViewCreateInfo& viewCI) {
	if (viewCI.viewType == vk::ImageViewType::e3D) { return true; }

	RenderTargetViews.reserve(viewCI.subresourceRange.layerCount);

	if ((imageCI.Usage & (vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eDepthStencilAttachment)) &&
	    ((viewCI.subresourceRange.levelCount > 1) || (viewCI.subresourceRange.layerCount > 1))) {
		auto viewInfo                        = viewCI;
		viewInfo.viewType                    = vk::ImageViewType::e2D;
		viewInfo.subresourceRange.levelCount = 1;
		viewInfo.subresourceRange.layerCount = 1;

		for (uint32_t layer = 0; layer < viewCI.subresourceRange.layerCount; ++layer) {
			viewInfo.subresourceRange.baseArrayLayer = layer + viewCI.subresourceRange.baseArrayLayer;
			vk::ImageView view                       = Parent.GetDevice().createImageView(viewInfo);
			RenderTargetViews.push_back(view);
		}
	}

	return true;
}
}  // namespace Vulkan
}  // namespace Luna
