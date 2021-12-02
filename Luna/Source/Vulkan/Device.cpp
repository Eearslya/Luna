#include <Luna/Core/Log.hpp>
#include <Luna/Threading/Threading.hpp>
#include <Luna/Vulkan/CommandBuffer.hpp>
#include <Luna/Vulkan/CommandPool.hpp>
#include <Luna/Vulkan/Context.hpp>
#include <Luna/Vulkan/Device.hpp>
#include <Luna/Vulkan/Fence.hpp>
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

	vmaDestroyAllocator(_allocator);

	DestroyTimelineSemaphores();
}

/* **********
 * Public Methods
 * ********** */

// ===== Frame management =====

void Device::EndFrame() {
	WAIT_FOR_PENDING_COMMAND_BUFFERS();
	EndFrameNoLock();
}

// Advance our frame context and get ready for new work submissions.
void Device::NextFrame() {
	WAIT_FOR_PENDING_COMMAND_BUFFERS();

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
void Device::Submit(CommandBufferHandle& cmd, FenceHandle* fence) {
	LOCK();
	SubmitNoLock(std::move(cmd), fence);
}

// ===== General Functionality =====

// The great big "make it go slow" button. This function will wait for all work on the GPU to be completed and perform
// some tidying up.
void Device::WaitIdle() {
	WAIT_FOR_PENDING_COMMAND_BUFFERS();
	WaitIdleNoLock();
}

// ===== Object Management =====

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
}

/* **********
 * Private Methods
 * ********** */

// ===== Frame management =====

void Device::EndFrameNoLock() {
	constexpr static const QueueType flushOrder[] = {QueueType::Transfer, QueueType::Graphics, QueueType::Compute};
	InternalFence submitFence;

	for (auto& type : flushOrder) {
		auto& queueData = _queueData[static_cast<int>(type)];
		if (queueData.NeedsFence || !Frame().Submissions[static_cast<int>(type)].empty()) {
			SubmitQueue(type, &submitFence);
			if (submitFence.Fence) {
				Frame().FencesToAwait.push_back(submitFence.Fence);
				Frame().FencesToRecycle.push_back(submitFence.Fence);
			}
			queueData.NeedsFence = false;
		}
	}
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
void Device::SubmitNoLock(CommandBufferHandle cmd, FenceHandle* fence) {
	const auto queueType = GetQueueType(cmd->GetType());
	auto& submissions    = Frame().Submissions[static_cast<int>(queueType)];

	// End command buffer recording.
	cmd->End();

	// Add this command buffer to the queue's list of submissions.
	submissions.push_back(std::move(cmd));

	// If we were given a fence to signal, we submit the queue now. If not, it can wait until the end of the frame.
	InternalFence submitFence;
	if (fence) {
		SubmitQueue(queueType, &submitFence);

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

void Device::SubmitQueue(QueueType queueType, InternalFence* submitFence) {
	auto& queueData   = _queueData[static_cast<int>(queueType)];
	auto& submissions = Frame().Submissions[static_cast<int>(queueType)];

	if (submissions.empty() && submitFence == nullptr) { return; }

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
	} else {
		if (submitFence) {
			submitFence->TimelineSemaphore = VK_NULL_HANDLE;
			submitFence->TimelineValue     = 0;
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
	for (auto& semaphore : SemaphoresToDestroy) { device.destroySemaphore(semaphore); }
	for (auto& semaphore : SemaphoresToRecycle) { Parent.ReleaseSemaphore(semaphore); }
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
