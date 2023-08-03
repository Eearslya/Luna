#include <Luna/Core/Threading.hpp>
#include <Luna/Vulkan/CommandBuffer.hpp>
#include <Luna/Vulkan/CommandPool.hpp>
#include <Luna/Vulkan/Context.hpp>
#include <Luna/Vulkan/Device.hpp>
#include <Luna/Vulkan/Fence.hpp>
#include <Luna/Vulkan/Semaphore.hpp>

namespace Luna {
namespace Vulkan {
#define DeviceLock()       std::lock_guard<std::mutex> _lockHolder(_lock.Lock)
#define DeviceMemoryLock() std::lock_guard<std::mutex> _lockHolderMemory(_lock.MemoryLock)
#define DeviceCacheLock()  RWSpinLockReadHolder _lockHolderCache(_lock.ReadOnlyCache)
#define DeviceFlush()                                   \
	std::unique_lock<std::mutex> _lockHolder(_lock.Lock); \
	_lock.Condition.wait(_lockHolder, [&]() { return _lock.Counter == 0; })

constexpr static const QueueType QueueFlushOrder[] = {QueueType::Transfer, QueueType::Graphics, QueueType::Compute};

Device::Device(Context& context)
		: _extensions(context._extensions),
			_instance(context._instance),
			_deviceInfo(context._deviceInfo),
			_queueInfo(context._queueInfo),
			_device(context._device) {
	if (_deviceInfo.EnabledFeatures.Vulkan12.timelineSemaphore) {
		const vk::SemaphoreCreateInfo semaphoreCI;
		const vk::SemaphoreTypeCreateInfo semaphoreType(vk::SemaphoreType::eTimeline, 0);
		const vk::StructureChain chain(semaphoreCI, semaphoreType);
		for (auto& queue : _queueData) {
			queue.TimelineSemaphore = _device.createSemaphore(chain.get());
			Log::Trace("Vulkan", "Timeline Semaphore created.");
		}
	}

	CreateFrameContexts(2);
}

Device::~Device() noexcept {
	WaitIdle();

	if (_deviceInfo.EnabledFeatures.Vulkan12.timelineSemaphore) {
		for (auto& queue : _queueData) {
			if (queue.TimelineSemaphore) {
				_device.destroySemaphore(queue.TimelineSemaphore);
				queue.TimelineSemaphore = nullptr;
			}
		}
	}

	_frameContexts.clear();
	for (auto& semaphore : _availableSemaphores) { _device.destroySemaphore(semaphore); }
	for (auto& fence : _availableFences) { _device.destroyFence(fence); }
}

/* ==============================================
** ===== Public Object Management Functions =====
*  ============================================== */
void Device::SetObjectName(vk::ObjectType type, uint64_t handle, const std::string& name) {
	if (!_extensions.DebugUtils) { return; }

	const vk::DebugUtilsObjectNameInfoEXT nameInfo(type, handle, name.c_str());
	_device.setDebugUtilsObjectNameEXT(nameInfo);
}

/* ============================================
** ===== Public Synchronization Functions =====
*  ============================================ */
void Device::NextFrame() {
	DeviceFlush();

	EndFrameNoLock();

	_currentFrameContext = (_currentFrameContext + 1) % _frameContexts.size();
	Frame().Begin();
}

CommandBufferHandle Device::RequestCommandBuffer(CommandBufferType type, const std::string& debugName) {
	return RequestCommandBufferForThread(Threading::GetThreadID(), type, debugName);
}

CommandBufferHandle Device::RequestCommandBufferForThread(uint32_t threadIndex,
                                                          CommandBufferType type,
                                                          const std::string& debugName) {
	DeviceLock();

	return RequestCommandBufferNoLock(threadIndex, type, debugName);
}

void Device::Submit(CommandBufferHandle& commandBuffer, FenceHandle* fence, std::vector<SemaphoreHandle>* semaphores) {
	DeviceLock();
	SubmitNoLock(std::move(commandBuffer), fence, semaphores);
}

void Device::WaitIdle() {
	DeviceFlush();
	WaitIdleNoLock();
}

/* ===============================================
** ===== Private Object Management Functions =====
*  =============================================== */
vk::Fence Device::AllocateFence() {
	if (_availableFences.empty()) {
		const vk::FenceCreateInfo fenceCI;

		return _device.createFence(fenceCI);
	} else {
		auto fence = _availableFences.back();
		_availableFences.pop_back();

		return fence;
	}
}

vk::Semaphore Device::AllocateSemaphore() {
	if (_availableSemaphores.empty()) {
		const vk::SemaphoreCreateInfo semaphoreCI;

		return _device.createSemaphore(semaphoreCI);
	} else {
		auto semaphore = _availableSemaphores.back();
		_availableSemaphores.pop_back();

		return semaphore;
	}
}

void Device::ConsumeSemaphore(vk::Semaphore semaphore) {
	DeviceLock();
	ConsumeSemaphoreNoLock(semaphore);
}

void Device::ConsumeSemaphoreNoLock(vk::Semaphore semaphore) {
	Frame().SemaphoresToConsume.push_back(semaphore);
}

void Device::DestroySemaphore(vk::Semaphore semaphore) {
	DeviceLock();
	DestroySemaphoreNoLock(semaphore);
}

void Device::DestroySemaphoreNoLock(vk::Semaphore semaphore) {
	Frame().SemaphoresToDestroy.push_back(semaphore);
}

void Device::FreeFence(vk::Fence fence) {
	_availableFences.push_back(fence);
}

void Device::FreeSemaphore(vk::Semaphore semaphore) {
	_availableSemaphores.push_back(semaphore);
}

void Device::ResetFence(vk::Fence fence, bool observedWait) {
	DeviceLock();
	ResetFenceNoLock(fence, observedWait);
}

void Device::ResetFenceNoLock(vk::Fence fence, bool observedWait) {
	if (observedWait) {
		_device.resetFences(fence);
		FreeFence(fence);
	} else {
		Frame().FencesToRecycle.push_back(fence);
	}
}

void Device::RecycleSemaphore(vk::Semaphore semaphore) {
	DeviceLock();
	RecycleSemaphoreNoLock(semaphore);
}

void Device::RecycleSemaphoreNoLock(vk::Semaphore semaphore) {
	Frame().SemaphoresToRecycle.push_back(semaphore);
}

/* =============================================
** ===== Private Synchronization Functions =====
*  ============================================= */
void Device::EndFrameNoLock() {
	for (const auto type : QueueFlushOrder) {
		if (_queueData[int(type)].NeedsFence || !Frame().Submissions[int(type)].empty() ||
		    !Frame().SemaphoresToConsume.empty()) {
			InternalFence fence = {};
			SubmitQueue(type, &fence, nullptr);
			if (fence.Fence) {
				Frame().FencesToAwait.push_back(fence.Fence);
				Frame().FencesToRecycle.push_back(fence.Fence);
			}
			_queueData[int(type)].NeedsFence = false;
		}
	}
}

void Device::FlushQueue(QueueType queueType) {
	if (!_queueInfo.Queue(queueType)) { return; }

	SubmitQueue(queueType, nullptr, nullptr);
}

CommandBufferHandle Device::RequestCommandBufferNoLock(uint32_t threadIndex,
                                                       CommandBufferType type,
                                                       const std::string& debugName) {
	const auto queueType = GetQueueType(type);
	auto& commandPool    = Frame().CommandPools[int(queueType)][threadIndex];
	auto commandBuffer   = commandPool.RequestCommandBuffer();
	++_lock.Counter;

	CommandBufferHandle handle(_commandBufferPool.Allocate(*this, type, commandBuffer, threadIndex, debugName));
	handle->Begin();

	return handle;
}

void Device::SubmitNoLock(CommandBufferHandle commandBuffer,
                          FenceHandle* fence,
                          std::vector<SemaphoreHandle>* semaphores) {
	const auto commandBufferType = commandBuffer->GetCommandBufferType();
	const auto queueType         = GetQueueType(commandBufferType);
	const bool hasSemaphores     = semaphores && semaphores->size() > 0;
	auto& submissions            = Frame().Submissions[int(queueType)];

	commandBuffer->End();
	submissions.push_back(std::move(commandBuffer));

	InternalFence signalFence;
	if (fence || hasSemaphores) { SubmitQueue(queueType, fence ? &signalFence : nullptr, semaphores); }

	if (fence) {
		if (signalFence.TimelineValue) {
			*fence = FenceHandle(_fencePool.Allocate(*this, signalFence.TimelineSemaphore, signalFence.TimelineValue));
		} else {
			*fence = FenceHandle(_fencePool.Allocate(*this, signalFence.Fence));
		}
	}

	--_lock.Counter;
	_lock.Condition.notify_all();
}

void Device::SubmitQueue(QueueType queueType, InternalFence* signalFence, std::vector<SemaphoreHandle>* semaphores) {
	// Ensure all pending operations on the transfer queue are submitted first.
	if (queueType != QueueType::Transfer) { FlushQueue(QueueType::Transfer); }

	// Gather a few useful locals.
	const bool hasSemaphores = semaphores && semaphores->size() > 0;
	auto& queueData          = _queueData[int(queueType)];
	auto& submissions        = Frame().Submissions[int(queueType)];
	vk::Queue queue          = _queueInfo.Queue(queueType);

	// Increment our timeline value for this queue.
	vk::Semaphore timelineSemaphore        = queueData.TimelineSemaphore;
	uint64_t timelineValue                 = ++queueData.TimelineValue;
	Frame().TimelineValues[int(queueType)] = queueData.TimelineValue;

	// Here, we batch all of the pending command buffers into as few submissions as possible. We should only need to make
	// a new batch if a signal semaphore is given.
	constexpr static const int MaxSubmissions = 8;
	struct SubmitBatch {
		std::vector<vk::CommandBufferSubmitInfo> CommandBuffers;
		std::vector<vk::SemaphoreSubmitInfo> SignalSemaphores;
		std::vector<vk::SemaphoreSubmitInfo> WaitSemaphores;
	};
	uint32_t currentBatch = 0;
	std::array<SubmitBatch, MaxSubmissions> batches;

	// Define a few helper functions to aid us in dealing with batches.
	const auto Batch     = [&]() -> SubmitBatch& { return batches[currentBatch]; };
	const auto NextBatch = [&]() -> void {
		if (Batch().WaitSemaphores.empty() && Batch().CommandBuffers.empty() && Batch().SignalSemaphores.empty()) {
			return;
		}

		++currentBatch;

		if (currentBatch >= MaxSubmissions) { throw std::runtime_error("Too many submission batches!"); }
	};
	const auto AddWaitSemaphore = [&](vk::Semaphore semaphore, uint64_t value, vk::PipelineStageFlags2 stages) -> void {
		if (!Batch().CommandBuffers.empty() || !Batch().SignalSemaphores.empty()) { NextBatch(); }

		Batch().WaitSemaphores.emplace_back(semaphore, value, stages);
	};
	const auto AddCommandBuffer = [&](vk::CommandBuffer commandBuffer) -> void {
		if (!Batch().SignalSemaphores.empty()) { NextBatch(); }

		Batch().CommandBuffers.emplace_back(commandBuffer);
	};
	const auto AddSignalSemaphore = [&](vk::Semaphore semaphore, uint64_t value, vk::PipelineStageFlags2 stages) -> void {
		Batch().SignalSemaphores.emplace_back(semaphore, value, stages);
	};

	// Gather all of the semaphores we need to wait on before beginning this submission.
	for (size_t i = 0; i < queueData.WaitSemaphores.size(); ++i) {
		auto& semaphoreHandle    = queueData.WaitSemaphores[i];
		const auto waitStages    = queueData.WaitStages[i];
		const auto timelineValue = semaphoreHandle->GetTimelineValue();
		const auto semaphore     = semaphoreHandle->Consume();

		Batch().WaitSemaphores.emplace_back(semaphore, timelineValue, waitStages);
		if (timelineValue == 0) { RecycleSemaphoreNoLock(semaphore); }
	}
	queueData.WaitSemaphores.clear();
	queueData.WaitStages.clear();

	// Add all of our command buffers.
	for (auto& commandBuffer : submissions) {
		const auto swapchainStages = commandBuffer->GetSwapchainStages();
		if (swapchainStages) {
		} else {
			AddCommandBuffer(commandBuffer->GetCommandBuffer());
		}
	}
	submissions.clear();

	// Add all of our consumed semaphores to the last batch, so the wait happens as late as possible.
	for (auto semaphore : Frame().SemaphoresToConsume) {
		AddWaitSemaphore(semaphore, 0, vk::PipelineStageFlagBits2::eNone);
		Frame().SemaphoresToRecycle.push_back(semaphore);
	}
	Frame().SemaphoresToConsume.clear();

	// Add our outgoing signal fences/semaphores, if applicable.
	if (_deviceInfo.EnabledFeatures.Vulkan12.timelineSemaphore) {
		AddSignalSemaphore(timelineSemaphore, timelineValue, vk::PipelineStageFlagBits2::eAllCommands);

		if (signalFence) {
			signalFence->Fence             = nullptr;
			signalFence->TimelineSemaphore = timelineSemaphore;
			signalFence->TimelineValue     = timelineValue;
		}

		if (hasSemaphores) {
			for (size_t i = 0; i < semaphores->size(); ++i) {
				(*semaphores)[i] = SemaphoreHandle(_semaphorePool.Allocate(*this, timelineSemaphore, timelineValue, false));
				(*semaphores)[i]->SignalExternal();
			}
		}
	} else {
		if (signalFence) {
			signalFence->Fence             = AllocateFence();
			signalFence->TimelineSemaphore = nullptr;
			signalFence->TimelineValue     = 0;
		}

		if (hasSemaphores) {
			for (size_t i = 0; i < semaphores->size(); ++i) {
				auto semaphore = AllocateSemaphore();
				AddSignalSemaphore(semaphore, 0, vk::PipelineStageFlagBits2::eAllCommands);
				(*semaphores)[i] = SemaphoreHandle(_semaphorePool.Allocate(*this, semaphore, true, true));
			}
		}
	}

	// Now we have everything together, we can build the list of submission structures.
	std::array<vk::SubmitInfo2, MaxSubmissions> submits;
	for (int i = 0; i < MaxSubmissions; ++i) {
		submits[i] = vk::SubmitInfo2({}, batches[i].WaitSemaphores, batches[i].CommandBuffers, batches[i].SignalSemaphores);
	}

	// Remove any empty submissions.
	uint32_t submitCount = 0;
	for (size_t i = 0; i < MaxSubmissions; ++i) {
		if (submits[i].waitSemaphoreInfoCount || submits[i].commandBufferInfoCount || submits[i].signalSemaphoreInfoCount) {
			if (i != submitCount) { submits[submitCount] = submits[i]; }
			++submitCount;
		}
	}

	// Finally, perform the actual queue submissions.
	if (_deviceInfo.EnabledFeatures.Synchronization2.synchronization2) {
		const auto submitResult = queue.submit2KHR(submitCount, submits.data(), signalFence ? signalFence->Fence : nullptr);
		if (submitResult != vk::Result::eSuccess) {
			Log::Error("Vulkan", "Failed to submit command buffers: {}", vk::to_string(submitResult));
		}
	} else {
		// If we don't have sync2, we need to convert the SubmitInfo2 structs back to normal SubmitInfo structs.
		for (int i = 0; i < submitCount; ++i) {
			const auto& submit    = submits[i];
			const vk::Fence fence = signalFence ? signalFence->Fence : nullptr;

			bool hasTimeline = false;
			std::vector<vk::CommandBuffer> commandBuffers(submit.commandBufferInfoCount);
			std::vector<vk::Semaphore> signalSemaphores(submit.signalSemaphoreInfoCount);
			std::vector<uint64_t> signalValues(submit.signalSemaphoreInfoCount);
			std::vector<vk::Semaphore> waitSemaphores(submit.waitSemaphoreInfoCount);
			std::vector<vk::PipelineStageFlags> waitStages(submit.waitSemaphoreInfoCount);
			std::vector<uint64_t> waitValues(submit.waitSemaphoreInfoCount);

			for (uint32_t i = 0; i < submit.waitSemaphoreInfoCount; ++i) {
				waitSemaphores[i] = submit.pWaitSemaphoreInfos[i].semaphore;
				waitStages[i]     = DowngradeDstPipelineStageFlags2(submit.pWaitSemaphoreInfos[i].stageMask);
				waitValues[i]     = submit.pWaitSemaphoreInfos[i].value;

				hasTimeline |= waitValues[i] != 0;
			}

			for (uint32_t i = 0; i < submit.commandBufferInfoCount; ++i) {
				commandBuffers[i] = submit.pCommandBufferInfos[i].commandBuffer;
			}

			for (uint32_t i = 0; i < submit.signalSemaphoreInfoCount; ++i) {
				signalSemaphores[i] = submit.pSignalSemaphoreInfos[i].semaphore;
				signalValues[i]     = submit.pSignalSemaphoreInfos[i].value;

				hasTimeline |= signalValues[i] != 0;
			}

			vk::SubmitInfo oldSubmit(waitSemaphores, waitStages, commandBuffers, signalSemaphores);
			vk::TimelineSemaphoreSubmitInfo oldTimeline(waitValues, signalValues);
			if (hasTimeline) { oldSubmit.pNext = &oldTimeline; }

			queue.submit(oldSubmit, i + 1 == submitCount ? fence : nullptr);
		}
	}

	if (!_deviceInfo.EnabledFeatures.Vulkan12.timelineSemaphore) { queueData.NeedsFence = true; }
}

/** Flushes all pending work and waits for the Vulkan device to be idle. */
void Device::WaitIdleNoLock() {
	// Flush all pending submissions, if we have any.
	if (!_frameContexts.empty()) { EndFrameNoLock(); }

	if (_device) {
		// Await the completion of all GPU work.
		_device.waitIdle();

		for (auto& queue : _queueData) {
			for (auto& semaphore : queue.WaitSemaphores) { _device.destroySemaphore(semaphore->Release()); }
			queue.WaitSemaphores.clear();
			queue.WaitStages.clear();
		}

		for (auto& frame : _frameContexts) {
			frame->FencesToAwait.clear();
			frame->Begin();
		}
	}

	// Now that the device is idle, we can guarantee that all pending work has been completed.
	// Therefore, we can remove the need to wait on any Semaphores or Fences.

	// First, we will remove all pending wait semaphores from our queues.
	for (auto& queue : _queueData) {
		for (auto& semaphore : queue.WaitSemaphores) { _device.destroySemaphore(semaphore->Consume()); }
		queue.WaitSemaphores.clear();
		queue.WaitStages.clear();
	}

	// Now, we remove all pending Fence waits from our frame contexts.
	// We can also use this time to clear our all of their deletion queues, as none of the objects are in use anymore.
	for (auto& context : _frameContexts) {
		context->FencesToAwait.clear();  // Remove all pending Fence waits.
		context->Begin();                // Destroy all objects pending destruction.
		context->Trim();                 // Trim Command Pools to optimize memory usage.
	}
}

/* ====================================
** ===== Private Helper Functions =====
*  ==================================== */
void Device::CreateFrameContexts(uint32_t count) {
	DeviceFlush();
	WaitIdleNoLock();

	_frameContexts.clear();
	for (uint32_t i = 0; i < count; ++i) { _frameContexts.emplace_back(new FrameContext(*this, i)); }
}

/** Return the current frame context. */
Device::FrameContext& Device::Frame() {
	return *_frameContexts[_currentFrameContext];
}

/** Return the Queue type that should be responsible for executing this command buffer type. */
QueueType Device::GetQueueType(CommandBufferType type) const {
	if (type != CommandBufferType::AsyncGraphics) { return static_cast<QueueType>(type); }

	if (_queueInfo.SameFamily(QueueType::Graphics, QueueType::Compute) &&
	    !_queueInfo.SameQueue(QueueType::Graphics, QueueType::Compute)) {
		return QueueType::Compute;
	}

	return QueueType::Graphics;
}

/* ==================================
** ===== FrameContext Functions =====
*  ================================== */
Device::FrameContext::FrameContext(Device& parent, uint32_t frameIndex) : Parent(parent), FrameIndex(frameIndex) {
	const auto threadCount = Threading::GetThreadCount();
	for (int type = 0; type < QueueTypeCount; ++type) {
		TimelineValues[type] = Parent._queueData[type].TimelineValue;

		CommandPools[type].reserve(threadCount);
		for (int i = 0; i < threadCount; ++i) {
			CommandPools[type].emplace_back(
				Parent, Parent._queueInfo.Families[type], fmt::format("{} Command Pool - Thread {}", QueueType(type), i));
		}
	}
}

Device::FrameContext::~FrameContext() noexcept {
	// Ensure our deletion queue is empty before we're fully destroyed.
	Begin();
}

void Device::FrameContext::Begin() {
	auto device = Parent._device;

	// First ensure whether we have Timeline Semaphores. Otherwise we need to use normal Fences.
	bool hasTimelineSemaphores = true;
	for (const auto& queue : Parent._queueData) {
		if (!queue.TimelineSemaphore) {
			hasTimelineSemaphores = false;
			break;
		}
	}

	// Await all timeline semaphores, if applicable.
	if (hasTimelineSemaphores) {
		// First gather all semaphores that don't have a 0 value.
		int semaphoreCount = 0;
		std::array<vk::Semaphore, QueueTypeCount> semaphores;
		std::array<uint64_t, QueueTypeCount> timelineValues;
		for (int i = 0; i < QueueTypeCount; ++i) {
			if (TimelineValues[i]) {
				semaphores[semaphoreCount]     = Parent._queueData[i].TimelineSemaphore;
				timelineValues[semaphoreCount] = TimelineValues[i];
				++semaphoreCount;
			}
		}

		// If any semaphores were found, wait on all of them at once.
		if (semaphoreCount) {
			const vk::SemaphoreWaitInfo waitInfo({}, semaphoreCount, semaphores.data(), timelineValues.data());
			const auto waitResult = device.waitSemaphores(waitInfo, std::numeric_limits<uint64_t>::max());
			if (waitResult != vk::Result::eSuccess) {
				Log::Error("Vulakn", "Failed to wait on semaphores: {}", vk::to_string(waitResult));
			}
		}
	}

	// Await all pending Fences.
	if (!FencesToAwait.empty()) {
		const auto waitResult = device.waitForFences(FencesToAwait, VK_TRUE, std::numeric_limits<uint64_t>::max());
		if (waitResult != vk::Result::eSuccess) { Log::Error("Vulkan", "Failed to wait on Fences"); }
		FencesToAwait.clear();
	}
	if (!FencesToRecycle.empty()) {
		device.resetFences(FencesToRecycle);
		for (auto& fence : FencesToRecycle) { Parent.FreeFence(fence); }
		FencesToRecycle.clear();
	}

	// Start all of our command pools.
	for (auto& queuePools : CommandPools) {
		for (auto& pool : queuePools) { pool.Begin(); }
	}

	// Clean up all deferred object deletions.
	for (auto semaphore : SemaphoresToDestroy) { device.destroySemaphore(semaphore); }
	for (auto semaphore : SemaphoresToRecycle) { Parent.FreeSemaphore(semaphore); }
	SemaphoresToDestroy.clear();
	SemaphoresToRecycle.clear();

	Log::Assert(SemaphoresToConsume.empty(), "Vulkan", "Not all semaphores were consumed");
}

void Device::FrameContext::Trim() {}
}  // namespace Vulkan
}  // namespace Luna
