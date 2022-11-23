#include <Luna/Vulkan/Device.hpp>
#include <Luna/Vulkan/Semaphore.hpp>

namespace Luna {
namespace Vulkan {
void SemaphoreDeleter::operator()(Semaphore* semaphore) {
	semaphore->_device._semaphorePool.Free(semaphore);
}

Semaphore::Semaphore(Device& device) : _device(device) {}

Semaphore::Semaphore(Device& device, vk::Semaphore semaphore, bool signalled, bool owned)
		: _device(device),
			_semaphore(semaphore),
			_signalled(signalled),
			_owned(owned),
			_semaphoreType(vk::SemaphoreType::eBinary) {}

Semaphore::Semaphore(Device& device, vk::Semaphore semaphore, uint64_t timeline, bool owned)
		: _device(device),
			_semaphore(semaphore),
			_timelineValue(timeline),
			_owned(owned),
			_semaphoreType(vk::SemaphoreType::eTimeline) {}

Semaphore::~Semaphore() noexcept {
	if (!_owned) { return; }

	if (_internalSync) {
		if (_semaphoreType == vk::SemaphoreType::eTimeline || _signalled) {
			_device.DestroySemaphoreNoLock(_semaphore);
		} else {
			_device.RecycleSemaphoreNoLock(_semaphore);
		}
	} else {
		if (_semaphoreType == vk::SemaphoreType::eTimeline || _signalled) {
			_device.DestroySemaphore(_semaphore);
		} else {
			_device.RecycleSemaphore(_semaphore);
		}
	}
}

vk::Semaphore Semaphore::Consume() {
	auto ret   = _semaphore;
	_semaphore = nullptr;
	_signalled = false;
	_owned     = false;

	return ret;
}

vk::Semaphore Semaphore::Release() {
	auto ret   = _semaphore;
	_semaphore = nullptr;
	_signalled = false;
	_owned     = false;

	return ret;
}

void Semaphore::SetPendingWait() {
	_pendingWait = true;
}

void Semaphore::SignalExternal() {
	_signalled = true;
}

void Semaphore::WaitExternal() {
	_signalled = false;
}
}  // namespace Vulkan
}  // namespace Luna
