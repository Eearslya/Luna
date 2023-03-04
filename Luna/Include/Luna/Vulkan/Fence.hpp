#pragma once

#include <Luna/Vulkan/Common.hpp>

namespace Luna {
namespace Vulkan {
struct FenceDeleter {
	void operator()(Fence* fence);
};

class Fence : public IntrusivePtrEnabled<Fence, FenceDeleter, HandleCounter>, public InternalSyncEnabled {
	friend class ObjectPool<Fence>;
	friend struct FenceDeleter;

 public:
	~Fence() noexcept;

	vk::Fence GetFence() const {
		return _fence;
	}
	bool HasObservedWait() const {
		return _observedWait;
	}

	void Wait();

 private:
	Fence(Device& device, vk::Fence fence);
	Fence(Device& device, vk::Semaphore timelineSemaphore, uint64_t timelineValue);

	Device& _device;
	vk::Fence _fence;
	vk::Semaphore _timelineSemaphore;
	uint64_t _timelineValue = 0;
	bool _observedWait      = false;
#ifdef LUNA_VULKAN_MT
	std::mutex _mutex;
#endif
};
}  // namespace Vulkan
}  // namespace Luna
