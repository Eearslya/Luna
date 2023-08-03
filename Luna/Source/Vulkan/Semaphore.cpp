#include <Luna/Vulkan/Device.hpp>
#include <Luna/Vulkan/Semaphore.hpp>

namespace Luna {
namespace Vulkan {
void SemaphoreDeleter::operator()(Semaphore* semaphore) {
	semaphore->_device._semaphorePool.Free(semaphore);
}

Semaphore::Semaphore(Device& device) : _device(device) {}

Semaphore::Semaphore(Device& device, vk::Semaphore semaphore, bool signalled, bool owned, const std::string& debugName)
		: _device(device),
			_semaphore(semaphore),
			_signalled(signalled),
			_owned(owned),
			_semaphoreType(vk::SemaphoreType::eBinary),
			_debugName(debugName) {}

Semaphore::Semaphore(
	Device& device, vk::Semaphore semaphore, uint64_t timelineValue, bool owned, const std::string& debugName)
		: _device(device),
			_semaphore(semaphore),
			_timelineValue(timelineValue),
			_owned(owned),
			_semaphoreType(vk::SemaphoreType::eTimeline),
			_debugName(debugName) {}

Semaphore::~Semaphore() noexcept {
	if (!_owned) { return; }

	// "Destroying" a semaphore can mean one of three things depending on the current state of the semaphore.
	//
	// Our implementation tries to recycle semaphores whenever possible, meaning we do not call vkDestroySemaphore and
	// simply mark the semaphore handle as available for any function that needs a semaphore later.
	// However, we cannot recycle timeline semaphores, so those are always destroyed immediately.
	//
	// If the semaphore has already been submitted for signalling, but this handle is being destroyed, it means nobody is
	// left to wait on it, so the semaphore is destroyed.
	// If the semaphore belongs to a "foreign" queue, such as the presentation engine, we cannot destroy it immediately.
	// We must first submit the semaphore to be waited on, then it will be recycled.
	//
	// Finally, if none of the above apply, the semaphore is submitted for recycling.

	if (_internalSync) {
		if (_semaphoreType == vk::SemaphoreType::eTimeline) {
			_device.DestroySemaphoreNoLock(_semaphore);
		} else if (_signalled) {
			if (_isForeignQueue) {
				_device.ConsumeSemaphoreNoLock(_semaphore);
			} else {
				_device.DestroySemaphoreNoLock(_semaphore);
			}
		} else {
			_device.RecycleSemaphoreNoLock(_semaphore);
		}
	} else {
		if (_semaphoreType == vk::SemaphoreType::eTimeline) {
			_device.DestroySemaphore(_semaphore);
		} else if (_signalled) {
			if (_isForeignQueue) {
				_device.ConsumeSemaphore(_semaphore);
			} else {
				_device.DestroySemaphore(_semaphore);
			}
		} else {
			_device.RecycleSemaphore(_semaphore);
		}
	}
}

vk::Semaphore Semaphore::Consume() {
	Log::Assert(_semaphore && _signalled, "Vulkan", "Attempting to Consume an invalid or unsignalled Semaphore");

	return Release();
}

vk::Semaphore Semaphore::Release() {
	auto sem   = _semaphore;
	_semaphore = nullptr;
	_signalled = false;
	_owned     = false;

	return sem;
}

void Semaphore::SetForeignQueue() {
	_isForeignQueue = true;
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
