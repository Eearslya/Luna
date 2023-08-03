#pragma once

#include <Luna/Vulkan/Common.hpp>

namespace Luna {
namespace Vulkan {
struct FenceDeleter {
	void operator()(Fence* fence);
};

class Fence : public VulkanObject<Fence, FenceDeleter>, public InternalSyncEnabled {
	friend struct FenceDeleter;
	friend class ObjectPool<Fence>;

 public:
	~Fence() noexcept;

	void Wait();
	bool TryWait(uint64_t timeout);

 private:
	Fence(Device& device, vk::Fence fence);
	Fence(Device& device, vk::Semaphore timelineSemaphore, uint64_t timelineValue);

	Device& _device;
	vk::Fence _fence;
	vk::Semaphore _timelineSemaphore;
	uint64_t _timelineValue;
	bool _observedWait = false;
	std::mutex _mutex;
};
}  // namespace Vulkan
}  // namespace Luna
