#include <Luna/Threading/Threading.hpp>
#include <Luna/Vulkan/Context.hpp>
#include <Luna/Vulkan/Device.hpp>

#ifdef LUNA_VULKAN_MT
static uint32_t GetThreadID() {
	return ::Luna::Threading::GetThreadID();
}
#	define LOCK() std::lock_guard<std::mutex> _DeviceLock(_sync.Mutex)
#	define WAIT_FOR_PENDING_COMMAND_BUFFERS()                                                                \
		do {                                                                                                    \
			using namespace std::chrono_literals;                                                                 \
			std::unique_lock<std::mutex> _DeviceLock(_sync.Mutex);                                                \
			if (!_sync.Condition.wait_for(_DeviceLock, 5s, [&]() { return _sync.PendingCommandBuffers == 0; })) { \
				throw std::runtime_error("Timed out waiting for all requested command buffers to be submitted!");   \
			}                                                                                                     \
		} while (0)
#else
static uint32_t GetThreadID() {
	return 0;
}
#	define LOCK()                             ((void) 0)
#	define WAIT_FOR_PENDING_COMMAND_BUFFERS() assert(_sync.PendingCommandBuffers == 0)
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

#ifdef LUNA_VULKAN_MT
	_nextCookie.store(0);
#endif
}

Device::~Device() noexcept {
	WaitIdle();
}

/* **********
 * Public Methods
 * ********** */

void Device::WaitIdle() {
	WAIT_FOR_PENDING_COMMAND_BUFFERS();
	WaitIdleNoLock();
}

/* **********
 * Private Methods
 * ********** */

void Device::WaitIdleNoLock() {
	if (_device) { _device.waitIdle(); }
}
}  // namespace Vulkan
}  // namespace Luna
