#include "Device.hpp"

#include "Buffer.hpp"
#include "CommandBuffer.hpp"
#include "CommandPool.hpp"
#include "Context.hpp"
#include "Fence.hpp"
#include "Semaphore.hpp"
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
	for (auto& allocation : MemoryToFree) { vmaFreeMemory(Parent._allocator, allocation); }
	for (auto& semaphore : SemaphoresToDestroy) { device.destroySemaphore(semaphore); }
	for (auto& semaphore : SemaphoresToRecycle) { Parent.ReleaseSemaphore(semaphore); }
	BuffersToDestroy.clear();
	MemoryToFree.clear();
	SemaphoresToDestroy.clear();
	SemaphoresToRecycle.clear();
}
}  // namespace Vulkan
}  // namespace Luna
