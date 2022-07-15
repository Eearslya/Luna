#include "Semaphore.hpp"

#include "Device.hpp"

namespace Luna {
namespace Vulkan {
void SemaphoreDeleter::operator()(Semaphore* semaphore) {
	semaphore->_device._semaphorePool.Free(semaphore);
}

Semaphore::Semaphore(Device& device) : _device(device), _semaphore(VK_NULL_HANDLE), _value(0) {}

Semaphore::Semaphore(Device& device, vk::Semaphore semaphore, bool signalled, const std::string& debugName)
		: _device(device), _semaphore(semaphore), _value(0), _signalled(signalled) {}

Semaphore::Semaphore(Device& device, vk::Semaphore semaphore, uint64_t value, const std::string& debugName)
		: _device(device), _semaphore(semaphore), _value(value) {}

Semaphore::~Semaphore() noexcept {
	if (_semaphore) {
		if (_internalSync) {
			if (_value > 0 || IsSignalled()) {
				_device.DestroySemaphoreNoLock(_semaphore);
			} else {
				_device.RecycleSemaphoreNoLock(_semaphore);
			}
		} else {
			if (_value > 0 || IsSignalled()) {
				_device.DestroySemaphore(_semaphore);
			} else {
				_device.RecycleSemaphore(_semaphore);
			}
		}
	}
}

vk::Semaphore Semaphore::Consume() {
	assert(_semaphore);
	assert(_signalled);

	return Release();
}

vk::Semaphore Semaphore::Release() {
	vk::Semaphore semaphore = _semaphore;
	_semaphore              = VK_NULL_HANDLE;
	_signalled              = false;

	return semaphore;
}

void Semaphore::SignalExternal() {
	assert(_semaphore);
	assert(!_signalled);

	_signalled = true;
}

void Semaphore::SignalPendingWait() {
	_pending = true;
}

void Semaphore::WaitExternal() {
	assert(_semaphore);
	assert(_signalled);

	_signalled = false;
}
}  // namespace Vulkan
}  // namespace Luna
