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

	vmaDestroyAllocator(_allocator);

	DestroyTimelineSemaphores();
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
	return {};
}

ImageHandle Device::CreateImageFromStagingBuffer(const ImageCreateInfo& imageCI, const ImageInitialBuffer* buffer) {
	return {};
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

SemaphoreHandle Device::RequestSemaphore() {
	DeviceLock();
	auto semaphore = AllocateSemaphore();
	return SemaphoreHandle(_semaphorePool.Allocate(*this, semaphore, false, true));
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
	} else {
	}
}

void Device::WaitIdleNoLock() {
	_device.waitIdle();
}

void Device::DestroyBuffer(vk::Buffer buffer) {
	DeviceLock();
	DestroyBufferNoLock(buffer);
}

void Device::DestroyBufferNoLock(vk::Buffer buffer) {
	Frame().BuffersToDestroy.push_back(buffer);
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
	for (auto& image : ImagesToDestroy) { device.destroyImage(image); }
	for (auto& view : ImageViewsToDestroy) { device.destroyImageView(view); }
	for (auto& allocation : AllocationsToFree) { vmaFreeMemory(Parent._allocator, allocation); }
	for (auto& semaphore : SemaphoresToDestroy) { device.destroySemaphore(semaphore); }
	for (auto& semaphore : SemaphoresToRecycle) { Parent.ReleaseSemaphore(semaphore); }
	BuffersToDestroy.clear();
	ImagesToDestroy.clear();
	ImageViewsToDestroy.clear();
	AllocationsToFree.clear();
	SemaphoresToDestroy.clear();
	SemaphoresToRecycle.clear();
}
}  // namespace Vulkan
}  // namespace Luna
