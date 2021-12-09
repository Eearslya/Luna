#include <Luna/Core/Log.hpp>
#include <Luna/Threading/Threading.hpp>
#include <Luna/Vulkan/Buffer.hpp>
#include <Luna/Vulkan/CommandBuffer.hpp>
#include <Luna/Vulkan/CommandPool.hpp>
#include <Luna/Vulkan/Context.hpp>
#include <Luna/Vulkan/Device.hpp>
#include <Luna/Vulkan/Fence.hpp>
#include <Luna/Vulkan/FormatLayout.hpp>
#include <Luna/Vulkan/Image.hpp>
#include <Luna/Vulkan/Semaphore.hpp>
#include <Luna/Vulkan/Swapchain.hpp>

// Helper functions for dealing with multithreading.
#ifdef LUNA_VULKAN_MT
static uint32_t GetThreadID() {
	return ::Luna::Threading::GetThreadID();
}
// One mutex to rule them all. This might be able to be optimized to several smaller mutexes at some point.
#	define LOCK() std::lock_guard<std::mutex> _DeviceLock(_mutex)
// Used for objects that can be internally synchronized. If the object has the internal sync flag, no lock is performed.
// Otherwise, the lock is made.
#	define MAYBE_LOCK(obj) \
		auto _DeviceLock = (obj->_internalSync ? std::unique_lock<std::mutex>() : std::unique_lock<std::mutex>(_mutex))

// As we hand out command buffers on request, we keep track of how many we have given out. Once we want to finalize the
// frame and move on, we need to make sure all of the command buffers we've given out have come back to us. This
// currently allows five seconds for all command buffers to return before giving up.
#	define WAIT_FOR_PENDING_COMMAND_BUFFERS()                                                                           \
		do {                                                                                                               \
			using namespace std::chrono_literals;                                                                            \
			std::unique_lock<std::mutex> _DeviceLock(_mutex);                                                                \
			if (!_pendingCommandBuffersCondition.wait_for(_DeviceLock, 5s, [&]() { return _pendingCommandBuffers == 0; })) { \
				throw std::runtime_error("Timed out waiting for all requested command buffers to be submitted!");              \
			}                                                                                                                \
		} while (0)
#else
static uint32_t GetThreadID() {
	return 0;
}
#	define LOCK()       ((void) 0)
#	define MAYBE_LOCK() ((void) 0)
#	define WAIT_FOR_PENDING_COMMAND_BUFFERS() \
		assert(_pendingCommandBuffers == 0 && "All command buffers must be submitted before end of frame!")
#endif

namespace Luna {
namespace Vulkan {
Device::Device(const Context& context)
		: _extensions(context.GetExtensionInfo()),
			_instance(context.GetInstance()),
			_surface(context.GetSurface()),
			_gpuInfo(context.GetGPUInfo()),
			_queues(context.GetQueueInfo()),
			_gpu(context.GetGPU()),
			_device(context.GetDevice()) {
	Threading::SetThreadID(0);

#define FN(name) .name                  = VULKAN_HPP_DEFAULT_DISPATCHER.name
	const VmaVulkanFunctions vmaFunctions = {FN(vkGetInstanceProcAddr),
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
	const VmaAllocatorCreateInfo allocatorCI = {.physicalDevice   = _gpu,
	                                            .device           = _device,
	                                            .frameInUseCount  = 1,
	                                            .pVulkanFunctions = &vmaFunctions,
	                                            .instance         = _instance,
	                                            .vulkanApiVersion = VK_API_VERSION_1_0};
	const auto allocatorResult               = vmaCreateAllocator(&allocatorCI, &_allocator);
	if (allocatorResult != VK_SUCCESS) {
		throw std::runtime_error("[Vulkan::Device] Failed to create memory allocator!");
	}

	CreateTimelineSemaphores();
	CreateFrameContexts(2);
}

Device::~Device() noexcept {
	WaitIdle();

	for (auto& fence : _availableFences) { _device.destroyFence(fence); }
	for (auto& semaphore : _availableSemaphores) { _device.destroySemaphore(semaphore); }

	_swapchainAcquire.Reset();
	_swapchainRelease.Reset();
	_swapchainImages.clear();

	vmaDestroyAllocator(_allocator);

	DestroyTimelineSemaphores();
}

/* **********
 * Public Methods
 * ********** */

// ===== Frame management =====

void Device::AddWaitSemaphore(CommandBufferType bufferType,
                              SemaphoreHandle semaphore,
                              vk::PipelineStageFlags stages,
                              bool flush) {
	LOCK();
	AddWaitSemaphoreNoLock(GetQueueType(bufferType), semaphore, stages, flush);
}

void Device::EndFrame() {
	WAIT_FOR_PENDING_COMMAND_BUFFERS();
	EndFrameNoLock();
}

// Advance our frame context and get ready for new work submissions.
void Device::NextFrame() {
	WAIT_FOR_PENDING_COMMAND_BUFFERS();

	EndFrameNoLock();

	_currentFrameContext = (_currentFrameContext + 1) % (_frameContexts.size());
	Frame().Begin();
}

// Request a command buffer from the specified queue. The returned command buffer will be started and ready to record
// immediately.
CommandBufferHandle Device::RequestCommandBuffer(CommandBufferType type) {
	LOCK();
	return RequestCommandBufferNoLock(type, GetThreadID());
}

// Submit a command buffer for processing. All command buffers retrieved from the device must be submitted on the same
// frame.
void Device::Submit(CommandBufferHandle& cmd, FenceHandle* fence, std::vector<SemaphoreHandle>* semaphores) {
	LOCK();
	SubmitNoLock(std::move(cmd), fence, semaphores);
}

// ===== General Functionality =====

// The great big "make it go slow" button. This function will wait for all work on the GPU to be completed and perform
// some tidying up.
void Device::WaitIdle() {
	WAIT_FOR_PENDING_COMMAND_BUFFERS();
	WaitIdleNoLock();
}

// ===== Object Management =====

BufferHandle Device::CreateBuffer(const BufferCreateInfo& createInfo, const void* initialData) {
	BufferCreateInfo actualInfo = createInfo;
	if (createInfo.Domain == BufferDomain::Device && initialData) {
		actualInfo.Usage |= vk::BufferUsageFlagBits::eTransferDst;
	}

	auto handle = BufferHandle(_bufferPool.Allocate(*this, actualInfo));

	if (createInfo.Domain == BufferDomain::Device && initialData && !handle->CanMap()) {
		auto stagingInfo   = createInfo;
		stagingInfo.Domain = BufferDomain::Host;
		stagingInfo.Usage |= vk::BufferUsageFlagBits::eTransferSrc;
		auto stagingBuffer = CreateBuffer(stagingInfo, initialData);

		auto transferCmd = RequestCommandBuffer(CommandBufferType::AsyncTransfer);
		transferCmd->CopyBuffer(*handle, *stagingBuffer);

		{
			LOCK();
			SubmitStaging(transferCmd, actualInfo.Usage, true);
		}
	} else if (initialData) {
		void* data = handle->Map();
		if (data) {
			memcpy(data, initialData, createInfo.Size);
			handle->Unmap();
		} else {
			Log::Error("[Vulkan::Device] Failed to map buffer!");
		}
	}

	return handle;
}

ImageHandle Device::CreateImage(const ImageCreateInfo& createInfo, const InitialImageData* initialData) {
	// If we have image data to put into this image, we first prepare a staging buffer.
	const bool generateMips = createInfo.Flags & ImageCreateFlagBits::GenerateMipmaps;
	InitialImageBuffer initialBuffer;
	if (initialData) {
		// If we want to generate mipmaps, we first need to determine how many levels there will be.
		uint32_t copyLevels = createInfo.MipLevels;
		if (generateMips) {
			copyLevels = 1;
		} else if (createInfo.MipLevels == 0) {
			copyLevels = CalculateMipLevels(createInfo.Extent);
		}

		Log::Trace("[Vulkan::Device] Creating initial image staging buffer.{}", generateMips ? " Generating mipmaps." : "");

		// Next, we need our helper class initialized with our image layout to help with calculating mip offsets.
		FormatLayout layout;
		switch (createInfo.Type) {
			case vk::ImageType::e1D:
				Log::Trace("[Vulkan::Device] - 1D Image Size: {}", createInfo.Extent.width);
				layout = FormatLayout(createInfo.Format, createInfo.Extent.width, createInfo.ArrayLayers, copyLevels);
				break;
			case vk::ImageType::e2D:
				Log::Trace("[Vulkan::Device] - 2D Image Size: {}x{}", createInfo.Extent.width, createInfo.Extent.height);
				layout = FormatLayout(createInfo.Format,
				                      vk::Extent2D(createInfo.Extent.width, createInfo.Extent.height),
				                      createInfo.ArrayLayers,
				                      copyLevels);
				break;
			case vk::ImageType::e3D:
				Log::Trace("[Vulkan::Device] - 3D Image Size: {}x{}x{}",
				           createInfo.Extent.width,
				           createInfo.Extent.height,
				           createInfo.Extent.depth);
				layout = FormatLayout(createInfo.Format, createInfo.Extent, copyLevels);
				break;
			default:
				return {};
		}

		Log::Trace("[Vulkan::Device] - Copying {} mip levels * {} array layers.", copyLevels, createInfo.ArrayLayers);
		Log::Trace("[Vulkan::Device] - Buffer requires {}B.", layout.GetRequiredSize());

		// Now we create the actual staging buffer.
		const BufferCreateInfo bufferCI(BufferDomain::Host,
		                                layout.GetRequiredSize(),
		                                vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eTransferSrc);
		initialBuffer.Buffer = CreateBuffer(bufferCI);

		// Map our data and set our layout class up.
		void* data     = initialBuffer.Buffer->Map();
		uint32_t index = 0;
		layout.SetBuffer(data, bufferCI.Size);

		// Now we do the copies of each mip level and array layer.
		for (uint32_t level = 0; level < copyLevels; ++level) {
			const auto& mipInfo          = layout.GetMipInfo(level);
			const size_t dstHeightStride = layout.GetLayerSize(level);
			const size_t rowSize         = layout.GetRowSize(level);

			for (uint32_t layer = 0; layer < createInfo.ArrayLayers; ++layer, ++index) {
				const uint32_t srcRowLength = initialData[index].RowLength ? initialData[index].RowLength : mipInfo.RowLength;
				const uint32_t srcArrayHeight =
					initialData[index].ImageHeight ? initialData[index].ImageHeight : mipInfo.ImageHeight;
				const uint32_t srcRowStride    = layout.RowByteStride(srcRowLength);
				const uint32_t srcHeightStride = layout.LayerByteStride(srcArrayHeight, srcRowStride);

				uint8_t* dst       = static_cast<uint8_t*>(layout.Data(layer, level));
				const uint8_t* src = static_cast<const uint8_t*>(initialData[index].Data);

				for (uint32_t z = 0; z < mipInfo.Extent.depth; ++z) {
					for (uint32_t y = 0; y < mipInfo.Extent.height; ++y) {
						memcpy(
							dst + (z * dstHeightStride) + (y * rowSize), src + (z * srcHeightStride) + (y * srcRowStride), rowSize);
					}
				}
			}
		}

		initialBuffer.Buffer->Unmap();
		initialBuffer.ImageCopies = layout.BuildBufferImageCopies();
	}

	// Now we adjust a few flags to ensure the image is created with all of the proper usages and flags before creating
	// the image itself.
	ImageCreateInfo actualInfo = createInfo;
	ImageHandle handle;
	{
		// If we have initial data, we need to be able to transfer into the image.
		if (initialData) { actualInfo.Usage |= vk::ImageUsageFlagBits::eTransferDst; }

		// If we are generating mips, we need to be able to transfer from each mip level.
		if (generateMips) { actualInfo.Usage |= vk::ImageUsageFlagBits::eTransferSrc; }

		// If the image domain is transient, ensure the transient attachment usage is applied.
		if (createInfo.Domain == ImageDomain::Transient) {
			actualInfo.Usage |= vk::ImageUsageFlagBits::eTransientAttachment;
		}

		// If the number of mips was specified as 0, calculate the correct number of mips based on image size.
		if (createInfo.MipLevels == 0) { actualInfo.MipLevels = CalculateMipLevels(createInfo.Extent); }

		// The initial layout given to the ImageCreateInfo struct is what we will transition to after creation. The image
		// still must be created as undefined.
		actualInfo.InitialLayout = vk::ImageLayout::eUndefined;

		handle = ImageHandle(_imagePool.Allocate(*this, actualInfo));
	}

	// If applicable, create a default ImageView.
	{
		const bool hasView(actualInfo.Usage &
		                   (vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eDepthStencilAttachment |
		                    vk::ImageUsageFlagBits::eInputAttachment | vk::ImageUsageFlagBits::eSampled |
		                    vk::ImageUsageFlagBits::eStorage));
		if (hasView) {
			const ImageViewCreateInfo viewCI{
				.Image = handle.Get(), .Format = createInfo.Format, .Type = GetImageViewType(createInfo)};
			auto view = CreateImageView(viewCI);
			handle->SetDefaultView(view);
		}
	}

	// Here we copy over the initial image data (if present) and transition the image to the layout requested in
	// ImageCreateInfo.
	{
		CommandBufferHandle transitionCmd;
		// Now that we have the image, we copy over the initial data if we have some.
		if (initialData) {
			vk::AccessFlags finalTransitionSrcAccess;
			if (generateMips) {
				finalTransitionSrcAccess = vk::AccessFlagBits::eTransferRead;
			} else if (_queues.SameQueue(QueueType::Graphics, QueueType::Transfer)) {
				finalTransitionSrcAccess = vk::AccessFlagBits::eTransferWrite;
			}
			const vk::AccessFlags prepareSrcAccess = _queues.SameQueue(QueueType::Graphics, QueueType::Transfer)
			                                           ? vk::AccessFlagBits::eTransferWrite
			                                           : vk::AccessFlags();
			bool needMipmapBarrier                 = true;
			bool needInitialBarrier                = generateMips;

			auto graphicsCmd = RequestCommandBuffer(CommandBufferType::Generic);
			CommandBufferHandle transferCmd;
			if (!_queues.SameQueue(QueueType::Graphics, QueueType::Transfer)) {
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
			transferCmd->CopyBufferToImage(*handle, *initialBuffer.Buffer, initialBuffer.ImageCopies);

			if (!_queues.SameQueue(QueueType::Graphics, QueueType::Transfer)) {
				vk::PipelineStageFlags dstStages =
					generateMips ? vk::PipelineStageFlagBits::eTransfer : handle->GetStageFlags();

				if (!_queues.SameFamily(QueueType::Graphics, QueueType::Transfer)) {
					needMipmapBarrier = false;

					const vk::ImageMemoryBarrier release(
						vk::AccessFlagBits::eTransferWrite,
						{},
						vk::ImageLayout::eTransferDstOptimal,
						generateMips ? vk::ImageLayout::eTransferSrcOptimal : createInfo.InitialLayout,
						_queues.Family(QueueType::Transfer),
						_queues.Family(QueueType::Graphics),
						handle->GetImage(),
						vk::ImageSubresourceRange(FormatToAspect(createInfo.Format),
					                            0,
					                            generateMips ? 1 : actualInfo.MipLevels,
					                            0,
					                            actualInfo.ArrayLayers));
					vk::ImageMemoryBarrier acquire = release;
					acquire.srcAccessMask          = {};
					acquire.dstAccessMask =
						generateMips ? vk::AccessFlagBits::eTransferRead
												 : (handle->GetAccessFlags() & ImageLayoutToPossibleAccess(createInfo.InitialLayout));

					transferCmd->Barrier(
						vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eBottomOfPipe, nullptr, nullptr, release);
					graphicsCmd->Barrier(dstStages, dstStages, nullptr, nullptr, acquire);
				}

				std::vector<SemaphoreHandle> semaphores(1);
				Submit(transferCmd, nullptr, &semaphores);
				AddWaitSemaphore(CommandBufferType::Generic, semaphores[0], dstStages, true);
			}

			if (generateMips) {
				graphicsCmd->GenerateMipmaps(*handle,
				                             vk::ImageLayout::eTransferDstOptimal,
				                             vk::PipelineStageFlagBits::eTransfer,
				                             prepareSrcAccess,
				                             needMipmapBarrier);
			}

			if (needInitialBarrier) {
				graphicsCmd->ImageBarrier(
					*handle,
					generateMips ? vk::ImageLayout::eTransferSrcOptimal : vk::ImageLayout::eTransferDstOptimal,
					createInfo.InitialLayout,
					vk::PipelineStageFlagBits::eTransfer,
					finalTransitionSrcAccess,
					handle->GetStageFlags(),
					handle->GetAccessFlags() & ImageLayoutToPossibleAccess(createInfo.InitialLayout));
			}

			transitionCmd = std::move(graphicsCmd);
		} else if (actualInfo.InitialLayout != vk::ImageLayout::eUndefined) {
			auto cmd = RequestCommandBuffer(CommandBufferType::Generic);
			cmd->ImageBarrier(*handle,
			                  actualInfo.InitialLayout,
			                  createInfo.InitialLayout,
			                  vk::PipelineStageFlagBits::eTopOfPipe,
			                  {},
			                  handle->GetStageFlags(),
			                  handle->GetAccessFlags() & ImageLayoutToPossibleAccess(createInfo.InitialLayout));
			transitionCmd = std::move(cmd);
		}

		if (transitionCmd) {
			LOCK();
			SubmitNoLock(transitionCmd, nullptr, nullptr);
		}
	}

	return handle;
}

ImageViewHandle Device::CreateImageView(const ImageViewCreateInfo& createInfo) {
	return ImageViewHandle(_imageViewPool.Allocate(*this, createInfo));
}

FenceHandle Device::RequestFence() {
	LOCK();
	auto fence = AllocateFence();
	return FenceHandle(_fencePool.Allocate(*this, fence));
}

SemaphoreHandle Device::RequestSemaphore(const std::string& debugName) {
	LOCK();
	auto semaphore = AllocateSemaphore();
	return SemaphoreHandle(_semaphorePool.Allocate(*this, semaphore, false, debugName));
}

// ===== Internal functions for other Vulkan classes =====

// Allocate a "cookie" to an object, which serves as a unique identifier for that object for the lifetime of the
// application.
uint64_t Device::AllocateCookie(Badge<Cookie>) {
#ifdef LUNA_VULKAN_MT
	return _nextCookie.fetch_add(16, std::memory_order_relaxed) + 16;
#else
	_nextCookie += 16;
	return _nextCookie;
#endif
}

SemaphoreHandle Device::ConsumeReleaseSemaphore(Badge<Swapchain>) {
	return std::move(_swapchainRelease);
}

void Device::DestroyBuffer(Badge<BufferDeleter>, Buffer* buffer) {
	MAYBE_LOCK(buffer);
	Frame().BuffersToDestroy.push_back(buffer);
}

void Device::DestroyImage(Badge<ImageDeleter>, Image* image) {
	MAYBE_LOCK(image);
	Frame().ImagesToDestroy.push_back(image);
}

void Device::DestroyImageView(Badge<ImageViewDeleter>, ImageView* view) {
	MAYBE_LOCK(view);
	Frame().ImageViewsToDestroy.push_back(view);
}

void Device::RecycleFence(Badge<FenceDeleter>, Fence* fence) {
	if (fence->GetFence()) {
		MAYBE_LOCK(fence);

		auto vkFence = fence->GetFence();

		if (fence->HasObservedWait()) {
			_device.resetFences(vkFence);
			ReleaseFence(vkFence);
		} else {
			Frame().FencesToRecycle.push_back(vkFence);
		}
	}

	_fencePool.Free(fence);
}

void Device::RecycleSemaphore(Badge<SemaphoreDeleter>, Semaphore* semaphore) {
	const auto vkSemaphore = semaphore->GetSemaphore();
	const auto value       = semaphore->GetTimelineValue();

	if (vkSemaphore && value == 0) {
		MAYBE_LOCK(semaphore);

		if (semaphore->IsSignalled()) {
			Frame().SemaphoresToDestroy.push_back(vkSemaphore);
		} else {
			Frame().SemaphoresToRecycle.push_back(vkSemaphore);
		}
	}

	_semaphorePool.Free(semaphore);
}

// Release a command buffer and return it to our pool.
void Device::ReleaseCommandBuffer(Badge<CommandBufferDeleter>, CommandBuffer* cmdBuf) {
	_commandBufferPool.Free(cmdBuf);
}

void Device::SetAcquireSemaphore(Badge<Swapchain>, uint32_t imageIndex, SemaphoreHandle& semaphore) {
	_swapchainAcquire         = std::move(semaphore);
	_swapchainAcquireConsumed = false;
	_swapchainIndex           = imageIndex;

	if (_swapchainAcquire) { _swapchainAcquire->_internalSync = true; }
}

void Device::SetupSwapchain(Badge<Swapchain>, Swapchain& swapchain) {
	WAIT_FOR_PENDING_COMMAND_BUFFERS();
	WaitIdleNoLock();

	const auto extent     = swapchain.GetExtent();
	const auto format     = swapchain.GetFormat();
	const auto& images    = swapchain.GetImages();
	const auto createInfo = ImageCreateInfo::RenderTarget(format, extent);

	for (const auto& image : images) {
		Image* img = _imagePool.Allocate(*this, createInfo, image);
		ImageHandle handle(img);
		const ImageViewCreateInfo viewCI{.Image = img, .Format = format, .Type = vk::ImageViewType::e2D};
		ImageViewHandle view(_imageViewPool.Allocate(*this, viewCI));
		handle->SetDefaultView(view);

		_swapchainImages.push_back(handle);
	}
}

/* **********
 * Private Methods
 * ********** */

// ===== Frame management =====

void Device::AddWaitSemaphoreNoLock(QueueType queueType,
                                    SemaphoreHandle semaphore,
                                    vk::PipelineStageFlags stages,
                                    bool flush) {
	if (flush) { FlushFrameNoLock(queueType); }

	auto& queueData = _queueData[static_cast<int>(queueType)];

	semaphore->SignalPendingWait();
	queueData.WaitSemaphores.push_back(semaphore);
	queueData.WaitStages.push_back(stages);
	queueData.NeedsFence = true;
}

void Device::EndFrameNoLock() {
	constexpr static const QueueType flushOrder[] = {QueueType::Transfer, QueueType::Graphics, QueueType::Compute};
	InternalFence submitFence;

	for (auto& type : flushOrder) {
		auto& queueData = _queueData[static_cast<int>(type)];
		if (queueData.NeedsFence || !Frame().Submissions[static_cast<int>(type)].empty()) {
			SubmitQueue(type, &submitFence, nullptr);
			if (submitFence.Fence) {
				Frame().FencesToAwait.push_back(submitFence.Fence);
				Frame().FencesToRecycle.push_back(submitFence.Fence);
			}
			queueData.NeedsFence = false;
		}
	}
}

void Device::FlushFrameNoLock(QueueType queueType) {
	if (_queues.Queue(queueType)) { SubmitQueue(queueType, nullptr, nullptr); }
}

// Return our current frame context.
Device::FrameContext& Device::Frame() {
	return *_frameContexts[_currentFrameContext];
}

// Private implementation of RequestCommandBuffer().
CommandBufferHandle Device::RequestCommandBufferNoLock(CommandBufferType type, uint32_t threadIndex) {
	const auto queueType = GetQueueType(type);
	auto& pool           = Frame().CommandPools[static_cast<int>(queueType)][threadIndex];
	auto buffer          = pool->RequestCommandBuffer();

	CommandBufferHandle handle(_commandBufferPool.Allocate(*this, buffer, type, threadIndex));
	handle->Begin();

	_pendingCommandBuffers++;

	return handle;
}

// Private implementation of Submit().
void Device::SubmitNoLock(CommandBufferHandle cmd, FenceHandle* fence, std::vector<SemaphoreHandle>* semaphores) {
	const auto queueType = GetQueueType(cmd->GetType());
	auto& submissions    = Frame().Submissions[static_cast<int>(queueType)];

	// End command buffer recording.
	cmd->End();

	// Add this command buffer to the queue's list of submissions.
	submissions.push_back(std::move(cmd));

	// If we were given a fence to signal, we submit the queue now. If not, it can wait until the end of the frame.
	InternalFence submitFence;
	if (fence || semaphores) {
		SubmitQueue(queueType, fence ? &submitFence : nullptr, semaphores);

		// Assign the fence handle appropriately, whether we're using fences or timeline semaphores.
		if (submitFence.TimelineValue != 0) {
			*fence = FenceHandle(_fencePool.Allocate(*this, submitFence.TimelineSemaphore, submitFence.TimelineValue));
		} else {
			*fence = FenceHandle(_fencePool.Allocate(*this, submitFence.Fence));
		}
	}

	// Signal to any threads that are waiting for all command buffers to be submitted.
	--_pendingCommandBuffers;
	_pendingCommandBuffersCondition.notify_all();
}

void Device::SubmitQueue(QueueType queueType, InternalFence* submitFence, std::vector<SemaphoreHandle>* semaphores) {
	auto& queueData          = _queueData[static_cast<int>(queueType)];
	auto& submissions        = Frame().Submissions[static_cast<int>(queueType)];
	const bool hasSemaphores = semaphores != nullptr && semaphores->size() != 0;

	if (submissions.empty() && submitFence == nullptr && !hasSemaphores) { return; }

	if (queueType != QueueType::Transfer) { FlushFrameNoLock(QueueType::Transfer); }

	vk::Queue queue                 = _queues.Queue(queueType);
	vk::Semaphore timelineSemaphore = queueData.TimelineSemaphore;
	uint64_t timelineValue          = ++queueData.TimelineValue;

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
	bool firstBatch = true;
	for (auto& cmdBufHandle : submissions) {
		// FIXME: Until command buffers are more fleshed out, we simply add the WSI acquire to the first batch.
		if (firstBatch) {
			if (_swapchainAcquire && !_swapchainAcquireConsumed) {
				batches[batch].WaitSemaphores.push_back(_swapchainAcquire->GetSemaphore());
				batches[batch].WaitStages.push_back(vk::PipelineStageFlagBits::eColorAttachmentOutput);
				batches[batch].WaitValues.push_back(_swapchainAcquire->GetTimelineValue());
				Frame().SemaphoresToRecycle.push_back(_swapchainAcquire->Consume());
				_swapchainAcquireConsumed = true;
				_swapchainAcquire.Reset();
			}
		}

		if (!batches[batch].SignalSemaphores.empty()) {
			++batch;
			assert(batch < MaxSubmissions);
		}

		batches[batch].CommandBuffers.push_back(cmdBufHandle->GetCommandBuffer());

		if (firstBatch) {
			vk::Semaphore release            = AllocateSemaphore();
			_swapchainRelease                = SemaphoreHandle(_semaphorePool.Allocate(*this, release, true));
			_swapchainRelease->_internalSync = true;
			batches[batch].SignalSemaphores.push_back(release);
			batches[batch].SignalValues.push_back(0);
		}

		firstBatch = false;
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
		Log::Error("[Vulkan::Device] Error occurred when submitting command buffers: {}", vk::to_string(submitResult));
	}

	// If we weren't able to use a timeline semaphore, we need to make sure there is a fence in
	// place to wait for completion.
	if (!_gpuInfo.AvailableFeatures.TimelineSemaphore.timelineSemaphore) { queueData.NeedsFence = true; }
}

void Device::SubmitStaging(CommandBufferHandle& cmd, vk::BufferUsageFlags usage, bool flush) {
	const auto access   = BufferUsageToAccess(usage);
	const auto stages   = BufferUsageToStages(usage);
	const auto srcQueue = _queues.Queue(GetQueueType(cmd->GetType()));

	if (_queues.SameQueue(QueueType::Graphics, QueueType::Compute)) {
		cmd->Barrier(vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferWrite, stages, access);
		SubmitNoLock(cmd, nullptr, nullptr);
	} else {
		const auto computeStages =
			stages & (vk::PipelineStageFlagBits::eComputeShader | vk::PipelineStageFlagBits::eDrawIndirect);
		const auto computeAccess = access & (vk::AccessFlagBits::eIndirectCommandRead | vk::AccessFlagBits::eShaderRead |
		                                     vk::AccessFlagBits::eShaderWrite | vk::AccessFlagBits::eTransferRead |
		                                     vk::AccessFlagBits::eTransferWrite | vk::AccessFlagBits::eUniformRead);

		const auto graphicsStages = stages & vk::PipelineStageFlagBits::eAllGraphics;

		if (srcQueue == _queues.Queue(QueueType::Graphics)) {
			cmd->Barrier(vk::PipelineStageFlagBits::eTransfer, vk::AccessFlagBits::eTransferWrite, graphicsStages, access);

			if (computeStages) {
				std::vector<SemaphoreHandle> semaphores(1);
				SubmitNoLock(cmd, nullptr, &semaphores);
				AddWaitSemaphoreNoLock(QueueType::Compute, semaphores[0], computeStages, flush);
			} else {
				SubmitNoLock(cmd, nullptr, nullptr);
			}
		} else if (srcQueue == _queues.Queue(QueueType::Compute)) {
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

// ===== General Functionality =====

// Helper function to determine the physical queue type to use for a command buffer.
QueueType Device::GetQueueType(CommandBufferType bufferType) const {
	if (bufferType == CommandBufferType::AsyncGraphics) {
		// For async graphics, if our graphics and compute queues are the same family, but different queues, we give the
		// compute queue. Otherwise, stick with the graphics queue.
		if (_queues.SameFamily(QueueType::Graphics, QueueType::Compute) &&
		    !_queues.SameIndex(QueueType::Graphics, QueueType::Compute)) {
			return QueueType::Compute;
		} else {
			return QueueType::Graphics;
		}
	}

	// For everything else, the CommandBufferType enum has the same values as the QueueType enum already.
	return static_cast<QueueType>(bufferType);
}

// Private implementation of WaitIdle().
void Device::WaitIdleNoLock() {
	// Make sure our current frame is completed.
	if (!_frameContexts.empty()) { EndFrameNoLock(); }

	// Wait on the actual device itself.
	if (_device) { _device.waitIdle(); }

	// Now that we know the device is doing nothing, we can go through all of our frame contexts and clean up all deferred
	// deletions.
	for (auto& context : _frameContexts) { context->Begin(); }
}

// ===== Internal setup and cleanup =====

vk::Fence Device::AllocateFence() {
	if (_availableFences.empty()) {
		Log::Trace("[Vulkan::Device] Creating new Fence.");

		const vk::FenceCreateInfo fenceCI;
		vk::Fence fence = _device.createFence(fenceCI);

		return fence;
	}

	vk::Fence fence = _availableFences.back();
	_availableFences.pop_back();

	return fence;
}

vk::Semaphore Device::AllocateSemaphore() {
	if (_availableSemaphores.empty()) {
		Log::Trace("[Vulkan::Device] Creating new Semaphore.");

		const vk::SemaphoreCreateInfo semaphoreCI;
		vk::Semaphore semaphore = _device.createSemaphore(semaphoreCI);

		return semaphore;
	}

	vk::Semaphore semaphore = _availableSemaphores.back();
	_availableSemaphores.pop_back();

	return semaphore;
}

// Reset and create our internal frame context objects.
void Device::CreateFrameContexts(uint32_t count) {
	Log::Debug("[Vulkan::Device] Creating {} frame contexts.", count);

	_currentFrameContext = 0;
	_frameContexts.clear();
	for (uint32_t i = 0; i < count; ++i) { _frameContexts.emplace_back(std::make_unique<FrameContext>(*this, i)); }
}

void Device::CreateTimelineSemaphores() {
	if (!_gpuInfo.AvailableFeatures.TimelineSemaphore.timelineSemaphore) { return; }

	const vk::SemaphoreCreateInfo semaphoreCI;
	const vk::SemaphoreTypeCreateInfo semaphoreType(vk::SemaphoreType::eTimeline, 0);
	const vk::StructureChain chain(semaphoreCI, semaphoreType);
	for (auto& queue : _queueData) {
		Log::Trace("[Vulkan::Device] Creating new Timeline Semaphore.");

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

void Device::ReleaseFence(vk::Fence fence) {
	_availableFences.push_back(fence);
}

void Device::ReleaseSemaphore(vk::Semaphore semaphore) {
	_availableSemaphores.push_back(semaphore);
}

#ifdef LUNA_DEBUG
void Device::SetObjectNameImpl(vk::ObjectType type, uint64_t handle, const std::string& name) {
	if (!_extensions.DebugUtils) { return; }

	const vk::DebugUtilsObjectNameInfoEXT nameInfo(type, handle, name.c_str());
	_device.setDebugUtilsObjectNameEXT(nameInfo);
}
#endif

/* **********
 * FrameContext Methods
 * ********** */
Device::FrameContext::FrameContext(Device& device, uint32_t frameIndex) : Parent(device), FrameIndex(frameIndex) {
	const auto threadCount = Threading::Get()->GetThreadCount();
	for (uint32_t type = 0; type < QueueTypeCount; ++type) {
		const auto family = Parent._queues.Families[type];
		for (uint32_t thread = 0; thread < threadCount; ++thread) {
			CommandPools[type].emplace_back(std::make_unique<CommandPool>(Parent, family));
		}
	}
}

Device::FrameContext::~FrameContext() noexcept {
	Begin();
}

// Start our frame of work. Here, we perform cleanup of everything we know is no longer in use.
void Device::FrameContext::Begin() {
	vk::Device device = Parent.GetDevice();

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
				if (Parent._queueData[i].TimelineValue) {
					semaphores[semaphoreCount] = Parent._queueData[i].TimelineSemaphore;
					values[semaphoreCount]     = Parent._queueData[i].TimelineValue;
					++semaphoreCount;
				}
			}

			if (semaphoreCount) {
				const vk::SemaphoreWaitInfo waitInfo({}, semaphoreCount, semaphores.data(), values.data());
				const auto waitResult = device.waitSemaphoresKHR(waitInfo, std::numeric_limits<uint64_t>::max());
				if (waitResult != vk::Result::eSuccess) {
					Log::Error("[Vulkan::Device] Failed to wait on timeline semaphores!");
				}
			}
		}
	}

	// Wait on our fences to ensure this frame context has completed all of its pending work.
	// If we are able to use timeline semaphores, this should never be needed.
	if (!FencesToAwait.empty()) {
		const auto waitResult = device.waitForFences(FencesToAwait, VK_TRUE, std::numeric_limits<uint64_t>::max());
		if (waitResult != vk::Result::eSuccess) { Log::Error("[Vulkan::Device] Failed to wait on submit fences!"); }
		FencesToAwait.clear();
	}

	// Reset all of the fences that we used this frame.
	if (!FencesToRecycle.empty()) {
		device.resetFences(FencesToRecycle);
		for (auto& fence : FencesToRecycle) { Parent.ReleaseFence(fence); }
		FencesToRecycle.clear();
	}

	// Reset all of our command pools.
	for (auto& queuePools : CommandPools) {
		for (auto& pool : queuePools) { pool->Reset(); }
	}

	// Destroy or recycle all of our other resources that are no longer in use.
	for (auto& buffer : BuffersToDestroy) { Parent._bufferPool.Free(buffer); }
	for (auto& image : ImagesToDestroy) { Parent._imagePool.Free(image); }
	for (auto& view : ImageViewsToDestroy) { Parent._imageViewPool.Free(view); }
	for (auto& semaphore : SemaphoresToDestroy) { device.destroySemaphore(semaphore); }
	for (auto& semaphore : SemaphoresToRecycle) { Parent.ReleaseSemaphore(semaphore); }
	BuffersToDestroy.clear();
	ImagesToDestroy.clear();
	ImageViewsToDestroy.clear();
	SemaphoresToDestroy.clear();
	SemaphoresToRecycle.clear();
}

// Trim our command pools to free up any unused memory they might still be holding onto.
void Device::FrameContext::TrimCommandPools() {
	for (auto& queuePools : CommandPools) {
		for (auto& pool : queuePools) { pool->Trim(); }
	}
}
}  // namespace Vulkan
}  // namespace Luna
