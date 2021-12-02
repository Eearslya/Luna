#include <Luna/Vulkan/Device.hpp>
#include <Luna/Vulkan/Semaphore.hpp>

namespace Luna {
namespace Vulkan {
void SemaphoreDeleter::operator()(Semaphore* semaphore) {
	semaphore->_device.RecycleSemaphore({}, semaphore);
}

Semaphore::Semaphore(Device& device) : _device(device), _semaphore(VK_NULL_HANDLE), _value(0) {}

Semaphore::Semaphore(Device& device, vk::Semaphore semaphore, bool signalled, const std::string& debugName)
		: _device(device), _semaphore(semaphore), _value(0), _signalled(signalled) {
#ifdef LUNA_DEBUG
	if (_semaphore && debugName.size() > 0) { _device.SetObjectName(_semaphore, debugName); }
#endif
}

Semaphore::Semaphore(Device& device, vk::Semaphore semaphore, uint64_t value, const std::string& debugName)
		: _device(device), _semaphore(semaphore), _value(value) {
#ifdef LUNA_DEBUG
	if (_semaphore && debugName.size() > 0) { _device.SetObjectName(_semaphore, debugName); }
#endif
}

Semaphore::~Semaphore() noexcept {
	_semaphore = nullptr;
	_value     = 0;
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

void Semaphore::WaitExternal() {
	assert(_semaphore);
	assert(_signalled);

	_signalled = false;
}
}  // namespace Vulkan
}  // namespace Luna
