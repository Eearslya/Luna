#pragma once

#include <Luna/Vulkan/Common.hpp>

namespace Luna {
namespace Vulkan {
struct SemaphoreDeleter {
	void operator()(Semaphore* semaphore);
};

class Semaphore : public IntrusivePtrEnabled<Semaphore, SemaphoreDeleter, HandleCounter>, public InternalSyncEnabled {
	friend class ObjectPool<Semaphore>;
	friend struct SemaphoreDeleter;

 public:
	~Semaphore() noexcept;

	vk::Semaphore GetSemaphore() const {
		return _semaphore;
	}
	vk::SemaphoreType GetSemaphoreType() const {
		return _semaphoreType;
	}
	uint64_t GetTimelineValue() const {
		return _timelineValue;
	}
	bool IsPendingWait() const {
		return _pendingWait;
	}
	bool IsSignalled() const {
		return _signalled;
	}

	vk::Semaphore Consume();
	vk::Semaphore Release();
	void SetPendingWait();
	void SignalExternal();
	void WaitExternal();

 private:
	explicit Semaphore(Device& device);
	Semaphore(Device& device, vk::Semaphore semaphore, bool signalled, bool owned);
	Semaphore(Device& device, vk::Semaphore semaphore, uint64_t timeline, bool owned);

	Device& _device;
	vk::Semaphore _semaphore;
	uint64_t _timelineValue          = 0;
	bool _owned                      = false;
	bool _pendingWait                = false;
	vk::SemaphoreType _semaphoreType = vk::SemaphoreType::eBinary;
	bool _signalled                  = false;
};
};  // namespace Vulkan
}  // namespace Luna
