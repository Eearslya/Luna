#include <Luna/Vulkan/Device.hpp>
#include <Luna/Vulkan/Semaphore.hpp>

namespace Luna {
namespace Vulkan {
void SemaphoreDeleter::operator()(Semaphore* semaphore) {
	semaphore->_device.RecycleSemaphore({}, semaphore);
}

Semaphore::Semaphore(Device& device) : _device(device), _semaphore(VK_NULL_HANDLE), _value(0) {}

Semaphore::Semaphore(Device& device, vk::Semaphore semaphore, bool signalled)
		: _device(device), _semaphore(semaphore), _value(0), _signalled(signalled) {}

Semaphore::Semaphore(Device& device, vk::Semaphore semaphore, uint64_t value)
		: _device(device), _semaphore(semaphore), _value(value) {}

Semaphore::~Semaphore() noexcept {}

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
}  // namespace Vulkan
}  // namespace Luna
