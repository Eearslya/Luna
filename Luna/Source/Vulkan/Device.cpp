#include <Luna/Core/Log.hpp>
#include <Luna/Threading/Threading.hpp>
#include <Luna/Vulkan/CommandBuffer.hpp>
#include <Luna/Vulkan/CommandPool.hpp>
#include <Luna/Vulkan/Context.hpp>
#include <Luna/Vulkan/Device.hpp>

// Helper functions for dealing with multithreading.
#ifdef LUNA_VULKAN_MT
static uint32_t GetThreadID() {
	return ::Luna::Threading::GetThreadID();
}
// One mutex to rule them all. This might be able to be optimized to several smaller mutexes at some point.
#	define LOCK() std::lock_guard<std::mutex> _DeviceLock(_mutex)

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
#	define LOCK() ((void) 0)
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

	CreateFrameContexts(2);
}

Device::~Device() noexcept {
	WaitIdle();
}

/* **********
 * Public Methods
 * ********** */

// ===== Frame management =====

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
void Device::Submit(CommandBufferHandle& cmd) {
	LOCK();
	SubmitNoLock(std::move(cmd));
}

// ===== General Functionality =====

// The great big "make it go slow" button. This function will wait for all work on the GPU to be completed and perform
// some tidying up.
void Device::WaitIdle() {
	WAIT_FOR_PENDING_COMMAND_BUFFERS();
	WaitIdleNoLock();
}

/* **********
 * Private Methods
 * ********** */

// ===== Frame management =====

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
void Device::SubmitNoLock(CommandBufferHandle cmd) {
	const auto queueType = GetQueueType(cmd->GetType());

	cmd->End();

	--_pendingCommandBuffers;
	_pendingCommandBuffersCondition.notify_all();
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
	// First, wait on the actual device itself.
	if (_device) { _device.waitIdle(); }

	// Now that we know the device is doing nothing, we can go through all of our frame contexts and clean up all deferred
	// deletions.
	for (auto& context : _frameContexts) { context->Begin(); }
}

// ===== Internal setup and cleanup =====

// Reset and create our internal frame context objects.
void Device::CreateFrameContexts(uint32_t count) {
	Log::Debug("[Vulkan::Device] Creating {} frame contexts.", count);

	_currentFrameContext = 0;
	_frameContexts.clear();
	for (uint32_t i = 0; i < count; ++i) { _frameContexts.emplace_back(std::make_unique<FrameContext>(*this, i)); }
}

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
	for (auto& queuePools : CommandPools) {
		for (auto& pool : queuePools) { pool->Reset(); }
	}
}

// Trim our command pools to free up any unused memory they might still be holding onto.
void Device::FrameContext::TrimCommandPools() {
	for (auto& queuePools : CommandPools) {
		for (auto& pool : queuePools) { pool->Trim(); }
	}
}
}  // namespace Vulkan
}  // namespace Luna
