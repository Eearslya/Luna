#include <Luna/Vulkan/Device.hpp>
#include <Luna/Vulkan/Fence.hpp>

namespace Luna {
namespace Vulkan {
void FenceDeleter::operator()(Fence* fence) {
	fence->_device._fencePool.Free(fence);
}

Fence::Fence(Device& device, vk::Fence fence)
		: _device(device), _fence(fence), _timelineSemaphore(nullptr), _timelineValue(0) {}

Fence::Fence(Device& device, vk::Semaphore timelineSemaphore, uint64_t timelineValue)
		: _device(device), _fence(nullptr), _timelineSemaphore(timelineSemaphore), _timelineValue(timelineValue) {}

Fence::~Fence() noexcept {
	if (_fence) {
		if (_internalSync) {
			_device.ResetFenceNoLock(_fence, _observedWait);
		} else {
			_device.ResetFence(_fence, _observedWait);
		}
	}
}

void Fence::Wait() {
	if (_observedWait) { return; }

	std::lock_guard<std::mutex> lock(_mutex);
	if (_timelineValue) {
		const vk::SemaphoreWaitInfo waitInfo({}, _timelineSemaphore, _timelineValue);
		const auto waitResult = _device._device.waitSemaphores(waitInfo, std::numeric_limits<uint64_t>::max());
		if (waitResult == vk::Result::eSuccess) {
			_observedWait = true;
		} else {
			Log::Error("Vulkan", "Failed to wait on Timeline Semaphore");
		}
	} else {
		const auto waitResult = _device._device.waitForFences(_fence, VK_TRUE, std::numeric_limits<uint64_t>::max());
		if (waitResult == vk::Result::eSuccess) {
			_observedWait = true;
		} else {
			Log::Error("Vulkan", "Failed to wait on Fence");
		}
	}
}

bool Fence::TryWait(uint64_t timeout) {
	if (_observedWait) { return true; }

	std::lock_guard<std::mutex> lock(_mutex);
	if (_timelineValue) {
		const vk::SemaphoreWaitInfo waitInfo({}, _timelineSemaphore, _timelineValue);
		const auto waitResult = _device._device.waitSemaphores(waitInfo, std::numeric_limits<uint64_t>::max());
		if (waitResult == vk::Result::eSuccess) {
			_observedWait = true;

			return true;
		} else {
			return false;
		}
	} else {
		const auto waitResult = _device._device.waitForFences(_fence, VK_TRUE, std::numeric_limits<uint64_t>::max());
		if (waitResult == vk::Result::eSuccess) {
			_observedWait = true;

			return true;
		} else {
			return false;
		}
	}
}
}  // namespace Vulkan
}  // namespace Luna
