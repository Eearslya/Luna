#include <Luna/Platform/Filesystem.hpp>
#include <Luna/Utility/Log.hpp>
#include <Luna/Utility/Threading.hpp>
#include <Luna/Vulkan/Buffer.hpp>
#include <Luna/Vulkan/BufferPool.hpp>
#include <Luna/Vulkan/CommandBuffer.hpp>
#include <Luna/Vulkan/CommandPool.hpp>
#include <Luna/Vulkan/Context.hpp>
#include <Luna/Vulkan/DescriptorSet.hpp>
#include <Luna/Vulkan/Device.hpp>
#include <Luna/Vulkan/Fence.hpp>
#include <Luna/Vulkan/Image.hpp>
#include <Luna/Vulkan/RenderPass.hpp>
#include <Luna/Vulkan/Sampler.hpp>
#include <Luna/Vulkan/Semaphore.hpp>
#include <Luna/Vulkan/Shader.hpp>
#include <Luna/Vulkan/ShaderCompiler.hpp>
#include <Luna/Vulkan/ShaderManager.hpp>
#include <Luna/Vulkan/WSI.hpp>

namespace Luna {
namespace Vulkan {
#ifdef LUNA_VULKAN_MT
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

constexpr QueueType QueueFlushOrder[] = {QueueType::Transfer, QueueType::Graphics, QueueType::Compute};

Device::Device(Context& context)
		: _extensions(context._extensions),
			_instance(context._instance),
			_deviceInfo(context._deviceInfo),
			_queueInfo(context._queueInfo),
			_device(context._device) {
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

		const VmaAllocatorCreateInfo allocatorCI = {.flags = _deviceInfo.EnabledFeatures.Vulkan12.bufferDeviceAddress
		                                                       ? VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT
		                                                       : VmaAllocatorCreateFlags{},
		                                            .physicalDevice   = _deviceInfo.PhysicalDevice,
		                                            .device           = _device,
		                                            .pVulkanFunctions = &vmaFunctions,
		                                            .instance         = _instance,
		                                            .vulkanApiVersion = VK_API_VERSION_1_2};
		const auto allocatorResult               = vmaCreateAllocator(&allocatorCI, &_allocator);
		if (allocatorResult != VK_SUCCESS) {
			throw std::runtime_error("[Vulkan::Device] Failed to create memory allocator!");
		}
	}

	CreateFrameContexts(2);
	CreatePipelineCache();
	CreateStockSamplers();
	CreateTimelineSemaphores();
	CreateTracingContexts();

	_framebufferAllocator         = std::make_unique<FramebufferAllocator>(*this);
	_shaderCompiler               = std::make_unique<ShaderCompiler>();
	_shaderManager                = std::make_unique<ShaderManager>(*this);
	_transientAttachmentAllocator = std::make_unique<TransientAttachmentAllocator>(*this);

	_indexBlocks = std::make_unique<BufferPool>(*this, 4 * 1024, 16, vk::BufferUsageFlagBits::eIndexBuffer, false);
	_indexBlocks->SetMaxRetainedBlocks(256);
	_uniformBlocks = std::make_unique<BufferPool>(
		*this,
		4 * 1024,
		std::max<vk::DeviceSize>(16, _deviceInfo.Properties.Core.limits.minUniformBufferOffsetAlignment),
		vk::BufferUsageFlagBits::eUniformBuffer,
		false);
	_uniformBlocks->SetSpillRegionSize(MaxUniformBufferSize);
	_uniformBlocks->SetMaxRetainedBlocks(64);
	_vertexBlocks = std::make_unique<BufferPool>(*this, 4 * 1024, 16, vk::BufferUsageFlagBits::eVertexBuffer, false);
	_vertexBlocks->SetMaxRetainedBlocks(256);
}

Device::~Device() noexcept {
	_swapchainAcquire.Reset();
	_swapchainRelease.Reset();
	_swapchainImages.clear();

	WaitIdle();

	if (_pipelineCache) {
		FlushPipelineCache();
		_device.destroyPipelineCache(_pipelineCache);
	}

	_vertexBlocks.reset();
	_uniformBlocks.reset();
	_indexBlocks.reset();

	_transientAttachmentAllocator.reset();
	_shaderManager.reset();
	_shaderCompiler.reset();
	_framebufferAllocator.reset();

	DestroyTracingContexts();
	DestroyTimelineSemaphores();
	_frameContexts.clear();
	for (auto& semaphore : _availableSemaphores) { _device.destroySemaphore(semaphore); }
	for (auto& fence : _availableFences) { _device.destroyFence(fence); }

	vmaDestroyAllocator(_allocator);
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

		default:
			throw std::runtime_error("Invalid image type given to GetImageViewType!");
	}
}

QueueType Device::GetQueueType(CommandBufferType cmdType) const {
	if (cmdType != CommandBufferType::AsyncGraphics) {
		// QueueType enum is made to match up with CommandBufferType.
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

bool Device::IsFormatSupported(vk::Format format, vk::FormatFeatureFlags features, vk::ImageTiling tiling) const {
	const auto props = _deviceInfo.PhysicalDevice.getFormatProperties(format);
	const auto featureFlags =
		tiling == vk::ImageTiling::eOptimal ? props.optimalTilingFeatures : props.linearTilingFeatures;

	return (featureFlags & features) == features;
}

void Device::AddWaitSemaphore(CommandBufferType cbType,
                              SemaphoreHandle semaphore,
                              vk::PipelineStageFlags2 stages,
                              bool flush) {
	DeviceLock();
	AddWaitSemaphoreNoLock(GetQueueType(cbType), semaphore, stages, flush);
}

void Device::EndFrame() {
	DeviceFlush();
	EndFrameNoLock();
}

void Device::FlushFrame() {
	DeviceLock();
	FlushFrameNoLock();
}

void Device::NextFrame() {
	DeviceFlush();

	EndFrameNoLock();

	_framebufferAllocator->BeginFrame();
	_transientAttachmentAllocator->BeginFrame();

	_currentFrameContext = (_currentFrameContext + 1) % _frameContexts.size();
	Frame().Begin();
}

CommandBufferHandle Device::RequestCommandBuffer(CommandBufferType type) {
	return RequestCommandBufferForThread(GetThreadIndex(), type);
}

CommandBufferHandle Device::RequestCommandBufferForThread(uint32_t threadIndex, CommandBufferType type) {
	DeviceLock();
	return RequestCommandBufferNoLock(threadIndex, type);
}

void Device::Submit(CommandBufferHandle& cmd, FenceHandle* fence, std::vector<SemaphoreHandle>* semaphores) {
	DeviceLock();
	SubmitNoLock(cmd, fence, semaphores);
}

void Device::WaitIdle() {
	DeviceFlush();
	WaitIdleNoLock();
}

BindlessDescriptorPoolHandle Device::CreateBindlessDescriptorPool(uint32_t setCount, uint32_t descriptorCount) {
	DescriptorSetLayout layout;
	layout.ArraySizes[0]    = DescriptorSetLayout::UnsizedArray;
	layout.FloatMask        = 1;
	layout.SampledImageMask = 1;

	const uint32_t stages[MaxDescriptorBindings] = {uint32_t(vk::ShaderStageFlagBits::eAll)};

	auto* allocator         = RequestDescriptorSetAllocator(layout, stages);
	vk::DescriptorPool pool = allocator->AllocateBindlessPool(setCount, descriptorCount);
	auto* handle            = _bindlessDescriptorPoolPool.Allocate(*this, allocator, pool, setCount, descriptorCount);

	return BindlessDescriptorPoolHandle(handle);
}

BufferHandle Device::CreateBuffer(const BufferCreateInfo& bufferInfo, const void* initial) {
	const bool zeroInit = bufferInfo.Flags & BufferCreateFlagBits::ZeroInitialize;
	if (initial && zeroInit) {
		throw std::logic_error("[Vulkan] Cannot create a buffer with initial data and zero-initialize flag set.");
	}
	if (bufferInfo.Size == 0) { throw std::logic_error("[Vulkan] Cannot create a buffer of 0 size."); }

	BufferCreateInfo actualInfo = bufferInfo;
	actualInfo.Usage |= vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eTransferSrc;

	const vk::BufferCreateInfo bufferCI({}, actualInfo.Size, actualInfo.Usage, vk::SharingMode::eExclusive, nullptr);

	VmaAllocationCreateInfo bufferAI{.usage = VMA_MEMORY_USAGE_AUTO};
	if (actualInfo.Domain == BufferDomain::Host) {
		bufferAI.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
	}

	VkBuffer buffer          = VK_NULL_HANDLE;
	VmaAllocation allocation = nullptr;
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
	Log::Trace("Vulkan", "Buffer created.");

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
			SubmitStaging(cmd, true);
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
	if (imageCI.Width == 0 || imageCI.Height == 0 || imageCI.Depth == 0) {
		throw std::logic_error("[Vulkan] Cannot create an image with 0 in any dimension.");
	}
	if (imageCI.Format == vk::Format::eUndefined) {
		throw std::logic_error("[Vulkan] Cannot create an image with Undefined format.");
	}
	if (imageCI.ArrayLayers == 0) { throw std::logic_error("[Vulkan] Cannot create an image with 0 array layers."); }

	vk::ImageFormatProperties properties = {};
	try {
		properties = _deviceInfo.PhysicalDevice.getImageFormatProperties(
			imageCI.Format, imageCI.Type, vk::ImageTiling::eOptimal, imageCI.Usage, imageCI.Flags);
	} catch (vk::FormatNotSupportedError& e) {
		Log::Fatal("Vulkan", "Cannot create image, format/usage/flags combination is unsupported by device.");
		Log::Fatal("Vulkan", "- Format: {}", vk::to_string(imageCI.Format));
		Log::Fatal("Vulkan", "- Usage: {}", vk::to_string(imageCI.Usage));
		Log::Fatal("Vulkan", "- Flags: {}", vk::to_string(imageCI.Flags));
		throw std::logic_error("[Vulkan] Failed to create image: Not supported by device.");
	}

	// Compatibility checks.
	if (imageCI.Width > properties.maxExtent.width || imageCI.Height > properties.maxExtent.height ||
	    imageCI.Depth > properties.maxExtent.depth) {
		Log::Fatal("Vulkan", "Cannot create image, dimensions exceed maximum limits for format.");
		Log::Fatal("Vulkan", "- Format: {}", vk::to_string(imageCI.Format));
		Log::Fatal("Vulkan", "- Image Size: {}x{}x{}", imageCI.Width, imageCI.Height, imageCI.Depth);
		Log::Fatal("Vulkan",
		           "- Maximum Size: {}x{}x{}",
		           properties.maxExtent.width,
		           properties.maxExtent.height,
		           properties.maxExtent.depth);
		throw std::logic_error("[Vulkan] Failed to create image, image is too large.");
	}
	if (imageCI.MipLevels != 0 && imageCI.MipLevels > properties.maxMipLevels) {
		Log::Fatal("Vulkan", "Cannot create image, exceeds maximum mip levels for format.");
		Log::Fatal("Vulkan", "- Format: {}", vk::to_string(imageCI.Format));
		Log::Fatal("Vulkan", "- Mip Levels: {}", imageCI.MipLevels);
		Log::Fatal("Vulkan", "- Maximum Levels: {}", properties.maxMipLevels);
		throw std::logic_error("[Vulkan] Failed to create image, too many mip levels.");
	}
	if (imageCI.ArrayLayers != 0 && imageCI.ArrayLayers > properties.maxMipLevels) {
		Log::Fatal("Vulkan", "Cannot create image, exceeds maximum array layers for format.");
		Log::Fatal("Vulkan", "- Format: {}", vk::to_string(imageCI.Format));
		Log::Fatal("Vulkan", "- Array Layers: {}", imageCI.ArrayLayers);
		Log::Fatal("Vulkan", "- Maximum Layers: {}", properties.maxArrayLayers);
		throw std::logic_error("[Vulkan] Failed to create image, too many array layers.");
	}
	if ((imageCI.Samples & properties.sampleCounts) != imageCI.Samples) {
		Log::Fatal("Vulkan", "Cannot create image, format does not support sample count.");
		Log::Fatal("Vulkan", "- Format: {}", vk::to_string(imageCI.Format));
		Log::Fatal("Vulkan", "- Sample Count: {}", vk::to_string(imageCI.Samples));
		Log::Fatal("Vulkan", "- Allowed Counts: {}", vk::to_string(properties.sampleCounts));
		throw std::logic_error("[Vulkan] Failed to create image, invalid sample counts.");
	}

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
	ImageCreateFlags queueFlags =
		imageCI.MiscFlags &
		(ImageCreateFlagBits::ConcurrentQueueGraphics | ImageCreateFlagBits::ConcurrentQueueAsyncCompute |
	   ImageCreateFlagBits::ConcurrentQueueAsyncGraphics | ImageCreateFlagBits::ConcurrentQueueAsyncTransfer);
	const bool concurrentQueue = bool(queueFlags) || buffer || imageCI.InitialLayout != vk::ImageLayout::eUndefined;
	if (concurrentQueue) {
		if (buffer && !bool(queueFlags)) {
			queueFlags |= ImageCreateFlagBits::ConcurrentQueueGraphics;
			queueFlags |= ImageCreateFlagBits::ConcurrentQueueAsyncGraphics;
			queueFlags |= ImageCreateFlagBits::ConcurrentQueueAsyncCompute;
			queueFlags |= ImageCreateFlagBits::ConcurrentQueueAsyncTransfer;
		} else if (buffer) {
			queueFlags |= ImageCreateFlagBits::ConcurrentQueueAsyncTransfer;
			if (imageCI.MiscFlags & ImageCreateFlagBits::GenerateMipmaps) {
				queueFlags |= ImageCreateFlagBits::ConcurrentQueueGraphics;
			}
		}

		struct Mapping {
			ImageCreateFlags Flags;
			QueueType Queue;
		};
		static constexpr Mapping Mappings[] = {
			{ImageCreateFlagBits::ConcurrentQueueGraphics | ImageCreateFlagBits::ConcurrentQueueAsyncGraphics,
		   QueueType::Graphics},
			{ImageCreateFlagBits::ConcurrentQueueAsyncCompute, QueueType::Compute},
			{ImageCreateFlagBits::ConcurrentQueueAsyncTransfer, QueueType::Transfer}};

		for (auto& map : Mappings) {
			if (queueFlags & map.Flags) { uniqueIndices.insert(_queueInfo.Family(map.Queue)); }
		}

		sharingIndices = std::vector<uint32_t>(uniqueIndices.begin(), uniqueIndices.end());
		if (sharingIndices.size() > 1) {
			createInfo.sharingMode = vk::SharingMode::eConcurrent;
			createInfo.setQueueFamilyIndices(sharingIndices);
		} else {
			createInfo.sharingMode = vk::SharingMode::eExclusive;
			createInfo.setQueueFamilyIndices(nullptr);
		}
	}

	if (!queueFlags) { queueFlags |= ImageCreateFlagBits::ConcurrentQueueGraphics; }

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
			return {};
		}
		Log::Trace("Vulkan", "Image created.");
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

	CommandBufferHandle transitionCmd;
	if (buffer) {
		const bool generateMips = imageCI.MiscFlags & ImageCreateFlagBits::GenerateMipmaps;

		{
			auto transferCmd = RequestCommandBuffer(CommandBufferType::AsyncTransfer);

			{
				LunaCmdZone(*transferCmd, "Upload Image Data");
				transferCmd->ImageBarrier(*handle,
				                          vk::ImageLayout::eUndefined,
				                          vk::ImageLayout::eTransferDstOptimal,
				                          vk::PipelineStageFlagBits2::eNone,
				                          vk::AccessFlagBits2::eNone,
				                          vk::PipelineStageFlagBits2::eCopy,
				                          vk::AccessFlagBits2::eTransferWrite);
				transferCmd->CopyBufferToImage(*handle, *buffer->Buffer, buffer->Blits);
				if (!generateMips) {
					transferCmd->ImageBarrier(*handle,
					                          vk::ImageLayout::eTransferDstOptimal,
					                          imageCI.InitialLayout,
					                          vk::PipelineStageFlagBits2::eCopy,
					                          vk::AccessFlagBits2::eTransferWrite,
					                          vk::PipelineStageFlagBits2::eNone,
					                          vk::AccessFlagBits2::eNone);
					transitionCmd = std::move(transferCmd);
				}
			}

			if (generateMips) {
				std::vector<SemaphoreHandle> semaphores(1);
				Submit(transferCmd, nullptr, &semaphores);
				AddWaitSemaphore(CommandBufferType::Generic, semaphores[0], vk::PipelineStageFlagBits2::eBlit, true);
			}
		}

		if (generateMips) {
			auto graphicsCmd = RequestCommandBuffer(CommandBufferType::Generic);
			LunaCmdZone(*graphicsCmd, "Generate Mipmaps");

			graphicsCmd->BarrierPrepareGenerateMipmaps(*handle,
			                                           vk::ImageLayout::eTransferDstOptimal,
			                                           vk::PipelineStageFlagBits2::eBlit,
			                                           vk::AccessFlagBits2::eNone,
			                                           true);
			graphicsCmd->GenerateMipmaps(*handle);

			graphicsCmd->ImageBarrier(*handle,
			                          vk::ImageLayout::eTransferSrcOptimal,
			                          imageCI.InitialLayout,
			                          vk::PipelineStageFlagBits2::eBlit,
			                          vk::AccessFlagBits2::eNone,
			                          vk::PipelineStageFlagBits2::eNone,
			                          vk::AccessFlagBits2::eNone);

			transitionCmd = std::move(graphicsCmd);
		}
	} else if (imageCI.InitialLayout != vk::ImageLayout::eUndefined) {
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

		transitionCmd = RequestCommandBuffer(type);
		transitionCmd->ImageBarrier(*handle,
		                            createInfo.initialLayout,
		                            imageCI.InitialLayout,
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
			if (GetQueueType(CommandBufferType::AsyncGraphics) == QueueType::Graphics) {
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
		}
		if (queueFlags & ImageCreateFlagBits::ConcurrentQueueAsyncGraphics) {
			types[semaphoreCount]  = CommandBufferType::AsyncGraphics;
			stages[semaphoreCount] = vk::PipelineStageFlagBits2::eAllCommands;
			semaphoreCount++;
		}
		if (queueFlags & ImageCreateFlagBits::ConcurrentQueueAsyncCompute) {
			types[semaphoreCount]  = CommandBufferType::AsyncCompute;
			stages[semaphoreCount] = vk::PipelineStageFlagBits2::eAllCommands;
			semaphoreCount++;
		}
		if (imageCI.MiscFlags & ImageCreateFlagBits::ConcurrentQueueAsyncTransfer) {
			types[semaphoreCount]  = CommandBufferType::AsyncTransfer;
			stages[semaphoreCount] = vk::PipelineStageFlagBits2::eAllCommands;
			semaphoreCount++;
		}

		std::vector<SemaphoreHandle> sem(semaphores.data(), semaphores.data() + semaphoreCount);
		Submit(transitionCmd, nullptr, &sem);
		for (uint32_t i = 0; i < semaphoreCount; ++i) { AddWaitSemaphore(types[i], sem[i], stages[i], true); }
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
	ImageManager manager(*this);
	const auto& imageCI = viewCI.Image->GetCreateInfo();

	const vk::Format format = viewCI.Format == vk::Format::eUndefined ? imageCI.Format : viewCI.Format;

	vk::ImageViewCreateInfo viewInfo(
		{},
		viewCI.Image->GetImage(),
		viewCI.ViewType,
		viewCI.Format,
		viewCI.Swizzle,
		vk::ImageSubresourceRange(
			FormatAspectFlags(viewCI.Format), viewCI.BaseLevel, viewCI.MipLevels, viewCI.BaseLayer, viewCI.ArrayLayers));

	if (viewInfo.subresourceRange.levelCount == VK_REMAINING_MIP_LEVELS) {
		viewInfo.subresourceRange.levelCount = imageCI.MipLevels - viewCI.BaseLevel;
	}
	if (viewInfo.subresourceRange.layerCount == VK_REMAINING_ARRAY_LAYERS) {
		viewInfo.subresourceRange.layerCount = imageCI.ArrayLayers - viewCI.BaseLayer;
	}

	if (!manager.CreateDefaultViews(imageCI, &viewInfo)) { return {}; }

	ImageViewCreateInfo tmpInfo = viewCI;
	tmpInfo.Format              = format;
	ImageViewHandle handle(_imageViewPool.Allocate(*this, manager.ImageView, tmpInfo));
	if (handle) {
		manager.Owned = false;
		handle->SetAltViews(manager.DepthView, manager.StencilView);
		handle->SetRenderTargetViews(std::move(manager.RenderTargetViews));

		return handle;
	}

	return {};
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

ImageView& Device::GetSwapchainView() {
	return GetSwapchainView(_swapchainIndex);
}

ImageView& Device::GetSwapchainView(uint32_t index) {
	return _swapchainImages[index]->GetView();
}

ImageHandle Device::GetTransientAttachment(const vk::Extent2D& extent,
                                           vk::Format format,
                                           uint32_t index,
                                           vk::SampleCountFlagBits samples,
                                           uint32_t arrayLayers) {
	return _transientAttachmentAllocator->RequestAttachment(extent, format, index, samples, arrayLayers);
}

DescriptorSetAllocator* Device::RequestDescriptorSetAllocator(const DescriptorSetLayout& layout,
                                                              const uint32_t* stagesForBindings) {
	Hasher h;
	h.Data(sizeof(DescriptorSetLayout), &layout);
	h.Data(sizeof(uint32_t) * MaxDescriptorBindings, stagesForBindings);
	const auto hash = h.Get();

	auto* ret = _descriptorSetAllocators.Find(hash);
	if (!ret) { ret = _descriptorSetAllocators.EmplaceYield(hash, hash, *this, layout, stagesForBindings); }

	return ret;
}

PipelineLayout* Device::RequestPipelineLayout(const ProgramResourceLayout& layout) {
	const auto hash = Hasher(layout).Get();

	auto* ret = _pipelineLayouts.Find(hash);
	if (!ret) { ret = _pipelineLayouts.EmplaceYield(hash, hash, *this, layout); }

	return ret;
}

Program* Device::RequestProgram(size_t compCodeSize, const void* compCode) {
	auto comp = RequestShader(compCodeSize, compCode);

	return RequestProgram(comp);
}

Program* Device::RequestProgram(size_t vertCodeSize, const void* vertCode, size_t fragCodeSize, const void* fragCode) {
	auto vert = RequestShader(vertCodeSize, vertCode);
	auto frag = RequestShader(fragCodeSize, fragCode);

	return RequestProgram(vert, frag);
}

Program* Device::RequestProgram(Shader* compute) {
	Hasher h;
	h(compute->GetHash());
	const auto hash = h.Get();

	Program* ret = _programs.Find(hash);
	if (!ret) {
		try {
			ret = _programs.EmplaceYield(hash, hash, *this, compute);
		} catch (const std::exception& e) {
			Log::Error("Vulkan", "Failed to create compute program: {}", e.what());
			return nullptr;
		}
	}

	return ret;
}

Program* Device::RequestProgram(const std::string& computeGlsl) {
	auto comp = RequestShader(vk::ShaderStageFlagBits::eCompute, computeGlsl);
	if (comp) {
		return RequestProgram(comp);
	} else {
		return nullptr;
	}
}

Program* Device::RequestProgram(Shader* vertex, Shader* fragment) {
	Hasher h;
	h(vertex->GetHash());
	h(fragment->GetHash());
	const auto hash = h.Get();

	Program* ret = _programs.Find(hash);
	if (!ret) {
		try {
			ret = _programs.EmplaceYield(hash, hash, *this, vertex, fragment);
		} catch (const std::exception& e) {
			Log::Error("Vulkan::Device", "Failed to create program: {}", e.what());
			return nullptr;
		}
	}

	return ret;
}

Program* Device::RequestProgram(const std::string& vertexGlsl, const std::string& fragmentGlsl) {
	auto vert = RequestShader(vk::ShaderStageFlagBits::eVertex, vertexGlsl);
	auto frag = RequestShader(vk::ShaderStageFlagBits::eFragment, fragmentGlsl);
	if (vert && frag) {
		return RequestProgram(vert, frag);
	} else {
		return nullptr;
	}
}

Sampler* Device::RequestSampler(const SamplerCreateInfo& createInfo) {
	Hasher h(createInfo);
	const auto hash = h.Get();
	auto* ret       = _samplers.Find(hash);
	if (!ret) { ret = _samplers.EmplaceYield(hash, hash, *this, createInfo); }

	return ret;
}

Sampler* Device::RequestSampler(StockSampler type) {
	return _stockSamplers[static_cast<int>(type)];
}

Shader* Device::RequestShader(size_t codeSize, const void* code) {
	Hasher h;
	h(codeSize);
	h.Data(codeSize, code);
	const auto hash = h.Get();

	auto* ret = _shaders.Find(hash);
	if (!ret) { ret = _shaders.EmplaceYield(hash, hash, *this, codeSize, code); }

	return ret;
}

Shader* Device::RequestShader(vk::ShaderStageFlagBits stage, const std::string& glsl) {
	auto spirv = _shaderCompiler->Compile(stage, glsl);
	if (spirv.has_value()) {
		return RequestShader(spirv.value().size() * sizeof(uint32_t), spirv.value().data());
	} else {
		return nullptr;
	}
}

Shader* Device::RequestShader(Hash hash) {
	return _shaders.Find(hash);
}

SemaphoreHandle Device::RequestSemaphore() {
	DeviceLock();
	auto semaphore = AllocateSemaphore();
	return SemaphoreHandle(_semaphorePool.Allocate(*this, semaphore, false, true));
}

void Device::AddWaitSemaphoreNoLock(QueueType queueType,
                                    SemaphoreHandle semaphore,
                                    vk::PipelineStageFlags2 stages,
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

void Device::CreateFrameContexts(uint32_t count) {
	DeviceFlush();
	WaitIdleNoLock();

	_frameContexts.clear();
	for (uint32_t i = 0; i < count; ++i) { _frameContexts.push_back(std::make_unique<FrameContext>(*this, i)); }
}

void Device::CreatePipelineCache() {
	constexpr static const auto uuidSize = sizeof(_deviceInfo.Properties.Core.pipelineCacheUUID);
	constexpr static const auto hashSize = sizeof(Hash);

	auto* filesystem = Filesystem::Get();
	auto cacheFile   = filesystem->OpenReadOnlyMapping("cache://PipelineCache.bin");

	const uint8_t* data = nullptr;
	size_t dataSize     = 0;
	if (cacheFile) {
		data     = cacheFile->Data<uint8_t>();
		dataSize = cacheFile->GetSize();
	}

	vk::PipelineCacheCreateInfo cacheCI({}, 0, nullptr);
	if (data && dataSize > (uuidSize + hashSize) &&
	    memcmp(data, _deviceInfo.Properties.Core.pipelineCacheUUID, uuidSize) == 0) {
		Hash refHash;
		memcpy(&refHash, data + uuidSize, hashSize);

		Hasher h;
		h.Data(dataSize - uuidSize - hashSize, data + uuidSize + hashSize);
		if (h.Get() == refHash) {
			Log::Debug("Vulkan", "Loading existing Pipeline Cache.");
			cacheCI.initialDataSize = dataSize - uuidSize - hashSize;
			cacheCI.pInitialData    = data + uuidSize + hashSize;
		}
	}

	if (_pipelineCache) { _device.destroyPipelineCache(_pipelineCache); }

	try {
		_pipelineCache = _device.createPipelineCache(cacheCI);
		Log::Trace("Vulkan", "Pipeline Cache created.");
	} catch (const vk::Error& vkError) {
		if (cacheCI.pInitialData) {
			cacheCI.initialDataSize = 0;
			cacheCI.pInitialData    = nullptr;
			try {
				_pipelineCache = _device.createPipelineCache(cacheCI);
				Log::Trace("Vulkan", "Pipeline Cache created.");
			} catch (const vk::Error& vkError) {
				Log::Error("Device", "Failed to initialize Pipeline Cache: {}", vkError.what());
			}
		} else {
			Log::Error("Device", "Failed to initialize Pipeline Cache: {}", vkError.what());
		}
	}
}

void Device::CreateStockSamplers() {
	for (int i = 0; i < StockSamplerCount; ++i) {
		const auto type = static_cast<StockSampler>(i);
		SamplerCreateInfo info{};
		info.MinLod = 0.0f;
		info.MaxLod = 12.0f;

		if (type == StockSampler::DefaultGeometryFilterClamp || type == StockSampler::DefaultGeometryFilterWrap ||
		    type == StockSampler::LinearClamp || type == StockSampler::LinearShadow || type == StockSampler::LinearWrap ||
		    type == StockSampler::TrilinearClamp || type == StockSampler::TrilinearWrap) {
			info.MagFilter = vk::Filter::eLinear;
			info.MinFilter = vk::Filter::eLinear;
		}

		if (type == StockSampler::DefaultGeometryFilterClamp || type == StockSampler::DefaultGeometryFilterWrap ||
		    type == StockSampler::LinearClamp || type == StockSampler::TrilinearClamp ||
		    type == StockSampler::TrilinearWrap) {
			info.MipmapMode = vk::SamplerMipmapMode::eLinear;
		}

		if (type == StockSampler::DefaultGeometryFilterClamp || type == StockSampler::LinearClamp ||
		    type == StockSampler::LinearShadow || type == StockSampler::NearestClamp ||
		    type == StockSampler::NearestShadow || type == StockSampler::TrilinearClamp) {
			info.AddressModeU = vk::SamplerAddressMode::eClampToEdge;
			info.AddressModeV = vk::SamplerAddressMode::eClampToEdge;
			info.AddressModeW = vk::SamplerAddressMode::eClampToEdge;
		}

		if (type == StockSampler::DefaultGeometryFilterClamp || type == StockSampler::DefaultGeometryFilterWrap) {
			if (_deviceInfo.EnabledFeatures.Core.samplerAnisotropy) {
				info.AnisotropyEnable = VK_TRUE;
				info.MaxAnisotropy    = std::min(_deviceInfo.Properties.Core.limits.maxSamplerAnisotropy, 16.0f);
			}
		}

		if (type == StockSampler::LinearShadow || type == StockSampler::NearestShadow) {
			info.CompareEnable = VK_TRUE;
			info.CompareOp     = vk::CompareOp::eLessOrEqual;
		}

		_stockSamplers[i] = RequestSampler(info);
	}
}

SemaphoreHandle Device::ConsumeReleaseSemaphore() {
	return std::move(_swapchainRelease);
}

void Device::CreateTimelineSemaphores() {
	if (!_deviceInfo.EnabledFeatures.Vulkan12.timelineSemaphore) { return; }

	const vk::SemaphoreCreateInfo semaphoreCI;
	const vk::SemaphoreTypeCreateInfo semaphoreType(vk::SemaphoreType::eTimeline, 0);
	const vk::StructureChain chain(semaphoreCI, semaphoreType);
	for (auto& queue : _queueData) {
		queue.TimelineSemaphore = _device.createSemaphore(chain.get());
		Log::Trace("Vulkan", "Timeline semaphore created.");
	}
}

void Device::CreateTracingContexts() {
#if TRACY_ENABLE
	for (uint32_t i = 0; i < QueueTypeCount; ++i) {
		const auto type        = static_cast<QueueType>(i);
		const auto familyProps = _deviceInfo.QueueFamilies[_queueInfo.Family(type)].queueFlags;
		// Tracing requires resetting query pools, which can only be done in Graphics or Compute.
		if (!(familyProps & (vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eCompute))) { continue; }

		auto& queue = _queueData[i];
		auto& frame = Frame();
		auto& pool  = frame.CommandPools[i][0];
		auto cmd    = pool->RequestCommandBuffer();

		if (_extensions.CalibratedTimestamps) {
			queue.TracingContext =
				TracyVkContextCalibrated(_deviceInfo.PhysicalDevice,
			                           _device,
			                           _queueInfo.Queue(type),
			                           cmd,
			                           VULKAN_HPP_DEFAULT_DISPATCHER.vkGetPhysicalDeviceCalibrateableTimeDomainsEXT,
			                           VULKAN_HPP_DEFAULT_DISPATCHER.vkGetCalibratedTimestampsEXT);
		} else {
			queue.TracingContext = TracyVkContext(_deviceInfo.PhysicalDevice, _device, _queueInfo.Queue(type), cmd);
		}

		if (queue.TracingContext) {
			std::vector<std::string> types;
			if (_queueInfo.Queue(type) == _queueInfo.Queue(QueueType::Graphics)) {
				types.push_back(VulkanEnumToString(QueueType::Graphics));
			}
			if (_queueInfo.Queue(type) == _queueInfo.Queue(QueueType::Transfer)) {
				types.push_back(VulkanEnumToString(QueueType::Transfer));
			}
			if (_queueInfo.Queue(type) == _queueInfo.Queue(QueueType::Compute)) {
				types.push_back(VulkanEnumToString(QueueType::Compute));
			}
			const std::string name = fmt::format("{} Queue", fmt::join(types, ", "));
			TracyVkContextName(queue.TracingContext, name.c_str(), name.size());
		}
	}

	WaitIdle();
#endif
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

void Device::DestroyTracingContexts() {
	for (auto& queue : _queueData) {
		if (queue.TracingContext) { TracyVkDestroy(queue.TracingContext); }
	}
}

void Device::FlushPipelineCache() {
	constexpr static const auto uuidSize = sizeof(_deviceInfo.Properties.Core.pipelineCacheUUID);
	constexpr static const auto hashSize = sizeof(Hash);

	if (!_pipelineCache) { return; }

	const auto cacheData = _device.getPipelineCacheData(_pipelineCache);
	if (cacheData.size() == 0) { return; }

	const auto fileSize = cacheData.size() + uuidSize + hashSize;
	Hasher h;
	h.Data(cacheData.size(), cacheData.data());
	const auto hash = h.Get();

	auto* filesystem = Filesystem::Get();
	auto cacheFile   = filesystem->OpenTransactionalMapping("cache://PipelineCache.bin", fileSize);
	auto* fileData   = cacheFile->MutableData<uint8_t>();

	memcpy(fileData, _deviceInfo.Properties.Core.pipelineCacheUUID, uuidSize);
	fileData += uuidSize;

	memcpy(fileData, &hash, hashSize);
	fileData += hashSize;

	memcpy(fileData, cacheData.data(), cacheData.size());
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
	colorFormats.fill(vk::Format::eUndefined);
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

	if (rpInfo.ArrayLayers > 1) {
		h(rpInfo.BaseLayer);
		h(rpInfo.ArrayLayers);
	} else {
		h(uint32_t(0));
		h(rpInfo.ArrayLayers);
	}
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

	const auto& extent = wsi._swapchainConfig.Extent;
	const auto& format = wsi._swapchainConfig.Format.format;
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
		Log::Trace("Vulkan", "Image View created.");

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
		if (_queueData[int(q)].NeedsFence || !Frame().Submissions[int(q)].empty() || !Frame().SemaphoresToConsume.empty()) {
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
	if (queueType == QueueType::Transfer) { SyncBufferBlocks(); }
	SubmitQueue(queueType, nullptr, nullptr);
}

void Device::FlushFrameNoLock() {
	for (const auto& t : QueueFlushOrder) { FlushFrame(t); }
}

CommandBufferHandle Device::RequestCommandBufferNoLock(uint32_t threadIndex, CommandBufferType type) {
	const auto queueType = GetQueueType(type);
	auto& cmdPool        = Frame().CommandPools[int(queueType)][threadIndex];
	auto cmdBuf          = cmdPool->RequestCommandBuffer();

	const vk::CommandBufferBeginInfo cmdBI(vk::CommandBufferUsageFlagBits::eOneTimeSubmit, nullptr);
	cmdBuf.begin(cmdBI);
	_lock.Counter++;
	CommandBufferHandle handle(
		_commandBufferPool.Allocate(*this, cmdBuf, type, threadIndex, _queueData[int(queueType)].TracingContext));

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

	if (queueType != QueueType::Transfer) { FlushFrame(QueueType::Transfer); }
	if (submissions.empty() && submitFence == nullptr && !hasSemaphores) { return; }

	vk::Queue queue                        = _queueInfo.Queue(queueType);
	vk::Semaphore timelineSemaphore        = queueData.TimelineSemaphore;
	uint64_t timelineValue                 = ++queueData.TimelineValue;
	Frame().TimelineValues[int(queueType)] = timelineValue;

	// Batch all of our command buffers into as few submissions as possible. Increment batch whenever we need to use a
	// signal semaphore.
	constexpr static const int MaxSubmissions = 8;
	struct SubmitBatch {
		bool HasTimeline = false;
		std::vector<vk::CommandBufferSubmitInfo> CommandBuffers;
		std::vector<vk::SemaphoreSubmitInfo> SignalSemaphores;
		std::vector<vk::SemaphoreSubmitInfo> WaitSemaphores;
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

		batches[batch].WaitSemaphores.push_back(vk::SemaphoreSubmitInfo(semaphore, waitValue, waitStages, 0));
		batches[batch].HasTimeline = batches[batch].HasTimeline || waitValue != 0;
	}
	queueData.WaitSemaphores.clear();
	queueData.WaitStages.clear();

	// Add our command buffers.
	for (auto& cmdBufHandle : submissions) {
		const vk::PipelineStageFlags2 swapchainStages = cmdBufHandle->GetSwapchainStages();

		if (swapchainStages && !_swapchainAcquireConsumed) {
			if (_swapchainAcquire && _swapchainAcquire->GetSemaphore()) {
				if (!batches[batch].CommandBuffers.empty() || !batches[batch].SignalSemaphores.empty()) { ++batch; }

				const auto value = _swapchainAcquire->GetTimelineValue();
				batches[batch].WaitSemaphores.push_back(
					vk::SemaphoreSubmitInfo(_swapchainAcquire->GetSemaphore(), value, swapchainStages));

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
			batches[batch].SignalSemaphores.push_back(vk::SemaphoreSubmitInfo(release, 0, {}, 0));
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

	for (auto semaphore : Frame().SemaphoresToConsume) {
		batches[batch].WaitSemaphores.push_back(
			vk::SemaphoreSubmitInfo(semaphore, 0, vk::PipelineStageFlagBits2::eNone, 0));
		Frame().SemaphoresToRecycle.push_back(semaphore);
	}
	Frame().SemaphoresToConsume.clear();

	// Emit any necessary semaphores from the final batch.
	if (_deviceInfo.EnabledFeatures.Vulkan12.timelineSemaphore) {
		batches[batch].SignalSemaphores.push_back(vk::SemaphoreSubmitInfo(timelineSemaphore, timelineValue, {}, 0));
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
				batches[batch].SignalSemaphores.push_back(vk::SemaphoreSubmitInfo(sem, 0, {}, 0));
				(*semaphores)[i] = SemaphoreHandle(_semaphorePool.Allocate(*this, sem, true, true));
			}
		}
	}

	// Build our submit info structures.
	std::array<vk::SubmitInfo2, MaxSubmissions> submits;
	for (uint8_t i = 0; i <= batch; ++i) {
		submits[i] = vk::SubmitInfo2({}, batches[i].WaitSemaphores, batches[i].CommandBuffers, batches[i].SignalSemaphores);
	}

	// Compact our submissions to remove any empty ones.
	uint32_t submitCount = 0;
	for (size_t i = 0; i < submits.size(); ++i) {
		if (submits[i].waitSemaphoreInfoCount || submits[i].commandBufferInfoCount || submits[i].signalSemaphoreInfoCount) {
			if (i != submitCount) { submits[submitCount] = submits[i]; }
			++submitCount;
		}
	}

	// Finally, submit it all!
	if (_deviceInfo.EnabledFeatures.Synchronization2.synchronization2) {
		const auto submitResult = queue.submit2(submitCount, submits.data(), fence);
		if (submitResult != vk::Result::eSuccess) {
			Log::Error("Vulkan", "Error occurred on command submission: {}", vk::to_string(submitResult));
		}
	} else {
		for (uint32_t i = 0; i < submitCount; ++i) {
			const auto& submit = submits[i];

			bool hasTimeline = false;
			std::vector<vk::CommandBuffer> commandBuffers;
			std::vector<vk::Semaphore> signalSemaphores;
			std::vector<uint64_t> signalValues;
			std::vector<vk::Semaphore> waitSemaphores;
			std::vector<vk::PipelineStageFlags> waitStages;
			std::vector<uint64_t> waitValues;

			for (uint32_t i = 0; i < submit.waitSemaphoreInfoCount; ++i) {
				waitSemaphores.push_back(submit.pWaitSemaphoreInfos[i].semaphore);
				waitStages.push_back(DowngradeDstPipelineStageFlags2(submit.pWaitSemaphoreInfos[i].stageMask));
				waitValues.push_back(submit.pWaitSemaphoreInfos[i].value);
				hasTimeline |= submit.pWaitSemaphoreInfos[i].value != 0;
			}
			for (uint32_t i = 0; i < submit.commandBufferInfoCount; ++i) {
				commandBuffers.push_back(submit.pCommandBufferInfos[i].commandBuffer);
			}
			for (uint32_t i = 0; i < submit.signalSemaphoreInfoCount; ++i) {
				signalSemaphores.push_back(submit.pSignalSemaphoreInfos[i].semaphore);
				signalValues.push_back(submit.pSignalSemaphoreInfos[i].value);
				hasTimeline |= submit.pSignalSemaphoreInfos[i].value != 0;
			}

			vk::SubmitInfo oldSubmit(waitSemaphores, waitStages, commandBuffers, signalSemaphores);
			vk::TimelineSemaphoreSubmitInfo oldTimeline;
			if (hasTimeline) {
				oldTimeline = vk::TimelineSemaphoreSubmitInfo(waitValues, signalValues);
				oldSubmit.setPNext(&oldTimeline);
			}

			queue.submit(oldSubmit, i + 1 == submitCount ? fence : nullptr);
		}
	}

	// If we weren't able to use a timeline semaphore, we need to make sure there is a fence in place to wait for
	// completion.
	if (!_deviceInfo.EnabledFeatures.Vulkan12.timelineSemaphore) { queueData.NeedsFence = true; }
}

void Device::SubmitStaging(CommandBufferHandle& cmd, bool flush) {
	std::vector<SemaphoreHandle> semaphores(2);
	SubmitNoLock(cmd, nullptr, &semaphores);
	semaphores[0]->SetInternalSync();
	semaphores[1]->SetInternalSync();
	AddWaitSemaphoreNoLock(QueueType::Graphics, semaphores[0], vk::PipelineStageFlagBits2::eAllCommands, flush);
	AddWaitSemaphoreNoLock(QueueType::Compute, semaphores[1], vk::PipelineStageFlagBits2::eAllCommands, flush);
}

void Device::SyncBufferBlocks() {
	if (_indexBlocksToCopy.empty() && _uniformBlocksToCopy.empty() && _vertexBlocksToCopy.empty()) { return; }

	auto cmd = RequestCommandBufferNoLock(GetThreadIndex(), CommandBufferType::AsyncTransfer);
	for (auto& block : _indexBlocksToCopy) { cmd->CopyBuffer(*block.Gpu, 0, *block.Cpu, 0, block.Offset); }
	for (auto& block : _uniformBlocksToCopy) { cmd->CopyBuffer(*block.Gpu, 0, *block.Cpu, 0, block.Offset); }
	for (auto& block : _vertexBlocksToCopy) { cmd->CopyBuffer(*block.Gpu, 0, *block.Cpu, 0, block.Offset); }
	_indexBlocksToCopy.clear();
	_uniformBlocksToCopy.clear();
	_vertexBlocksToCopy.clear();
	SubmitStaging(cmd, false);
}

void Device::WaitIdleNoLock() {
	if (!_frameContexts.empty()) { EndFrameNoLock(); }

	_device.waitIdle();

	if (_indexBlocks) { _indexBlocks->Reset(); }
	if (_uniformBlocks) { _uniformBlocks->Reset(); }
	if (_vertexBlocks) { _vertexBlocks->Reset(); }
	for (auto& frame : _frameContexts) {
		frame->IndexBlocks.clear();
		frame->UniformBlocks.clear();
		frame->VertexBlocks.clear();
	}

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

void Device::RequestBlock(BufferBlock& block,
                          vk::DeviceSize size,
                          BufferPool& pool,
                          std::vector<BufferBlock>& copies,
                          std::vector<BufferBlock>& recycles) {
	if (block.Offset == 0) {
		if (block.Size == pool.GetBlockSize()) { pool.RecycleBlock(block); }
	} else {
		if (block.Cpu != block.Gpu) { copies.push_back(block); }

		if (block.Size == pool.GetBlockSize()) { recycles.push_back(block); }
	}

	if (size) {
		block = pool.RequestBlock(size);
	} else {
		block = {};
	}
}

void Device::RequestIndexBlock(BufferBlock& block, vk::DeviceSize size) {
	DeviceLock();
	RequestIndexBlockNoLock(block, size);
}

void Device::RequestIndexBlockNoLock(BufferBlock& block, vk::DeviceSize size) {
	RequestBlock(block, size, *_indexBlocks, _indexBlocksToCopy, Frame().IndexBlocks);
}

void Device::RequestUniformBlock(BufferBlock& block, vk::DeviceSize size) {
	DeviceLock();
	RequestUniformBlockNoLock(block, size);
}

void Device::RequestUniformBlockNoLock(BufferBlock& block, vk::DeviceSize size) {
	RequestBlock(block, size, *_uniformBlocks, _uniformBlocksToCopy, Frame().UniformBlocks);
}

void Device::RequestVertexBlock(BufferBlock& block, vk::DeviceSize size) {
	DeviceLock();
	RequestVertexBlockNoLock(block, size);
}

void Device::RequestVertexBlockNoLock(BufferBlock& block, vk::DeviceSize size) {
	RequestBlock(block, size, *_vertexBlocks, _vertexBlocksToCopy, Frame().VertexBlocks);
}

void Device::ConsumeSemaphore(vk::Semaphore semaphore) {
	DeviceLock();
	ConsumeSemaphoreNoLock(semaphore);
}

void Device::ConsumeSemaphoreNoLock(vk::Semaphore semaphore) {
	Frame().SemaphoresToConsume.push_back(semaphore);
}

void Device::DestroyBuffer(vk::Buffer buffer) {
	DeviceLock();
	DestroyBufferNoLock(buffer);
}

void Device::DestroyBufferNoLock(vk::Buffer buffer) {
	Frame().BuffersToDestroy.push_back(buffer);
}

void Device::DestroyDescriptorPool(vk::DescriptorPool pool) {
	DeviceLock();
	DestroyDescriptorPoolNoLock(pool);
}

void Device::DestroyDescriptorPoolNoLock(vk::DescriptorPool pool) {
	Frame().DescriptorPoolsToDestroy.push_back(pool);
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

	const uint32_t threadCount = Threading::Get()->GetThreadCount() + 1;
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

	// Wait on timeline semaphores, or fences if they're not available.
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

	for (auto& block : IndexBlocks) { Parent._indexBlocks->RecycleBlock(block); }
	for (auto& block : UniformBlocks) { Parent._uniformBlocks->RecycleBlock(block); }
	for (auto& block : VertexBlocks) { Parent._vertexBlocks->RecycleBlock(block); }
	IndexBlocks.clear();
	UniformBlocks.clear();
	VertexBlocks.clear();

	for (auto& buffer : BuffersToDestroy) { device.destroyBuffer(buffer); }
	for (auto& pool : DescriptorPoolsToDestroy) { device.destroyDescriptorPool(pool); }
	for (auto& framebuffer : FramebuffersToDestroy) { device.destroyFramebuffer(framebuffer); }
	for (auto& image : ImagesToDestroy) { device.destroyImage(image); }
	for (auto& view : ImageViewsToDestroy) { device.destroyImageView(view); }
	for (auto& allocation : AllocationsToFree) { vmaFreeMemory(Parent._allocator, allocation); }
	for (auto& semaphore : SemaphoresToDestroy) { device.destroySemaphore(semaphore); }
	for (auto& semaphore : SemaphoresToRecycle) { Parent.ReleaseSemaphore(semaphore); }
	BuffersToDestroy.clear();
	DescriptorPoolsToDestroy.clear();
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
		Log::Trace("Vulkan", "Image View created.");

		tmpCI.format = viewFormats[1];
		SrgbView     = device.createImageView(tmpCI);
		Log::Trace("Vulkan", "Image View created.");
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
			Log::Trace("Vulkan", "Image View created.");

			viewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eStencil;
			StencilView                          = Parent.GetDevice().createImageView(viewInfo);
			Log::Trace("Vulkan", "Image View created.");
		}
	}

	return true;
}

bool Device::ImageManager::CreateDefaultView(const vk::ImageViewCreateInfo& viewCI) {
	DefaultViewType = viewCI.viewType;
	ImageView       = Parent.GetDevice().createImageView(viewCI);
	Log::Trace("Vulkan", "Image View created.");

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
			Log::Trace("Vulkan", "Image View created.");
			RenderTargetViews.push_back(view);
		}
	}

	return true;
}
}  // namespace Vulkan
}  // namespace Luna
