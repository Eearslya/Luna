#include <Luna/Threading/Threading.hpp>
#include <Luna/Vulkan/Context.hpp>
#include <Luna/Vulkan/Device.hpp>

#ifdef LUNA_VULKAN_MT
static uint32_t GetThreadID() {
	return ::Luna::Threading::GetThreadID();
}
#	define LOCK() std::lock_guard<std::mutex> _DeviceLock(_sync.Mutex)
#	define DRAIN_FRAME_LOCK()                                                   \
		do {                                                                       \
			std::unique_lock<std::mutex> _DeviceLock(_sync.Mutex);                   \
			_sync.Condition.wait(_DeviceLock, [&]() { return _sync.Counter == 0; }); \
		} while (0)
#else
static uint32_t GetThreadID() {
	return 0;
}
#	define LOCK()             ((void) 0)
#	define DRAIN_FRAME_LOCK() assert(_sync.Counter == 0)
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

void Device::WaitIdle() {
	DRAIN_FRAME_LOCK();
	WaitIdleNoLock();
}

void Device::WaitIdleNoLock() {
	if (_device) { _device.waitIdle(); }
}
}  // namespace Vulkan
}  // namespace Luna
