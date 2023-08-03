#pragma once

#include <Luna/Vulkan/Common.hpp>

namespace Luna {
namespace Vulkan {
struct SemaphoreDeleter {
	void operator()(Semaphore* semaphore);
};

class Semaphore : public VulkanObject<Semaphore, SemaphoreDeleter>, public InternalSyncEnabled {
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

	/**
	 * Consume the Semaphore handle.
	 * Semaphore handle must exist and have been signalled.
	 *
	 * @return The vk::Semaphore handle.
	 */
	vk::Semaphore Consume();

	/**
	 * Release the Semaphore handle.
	 *
	 * @return The vk::Semaphore handle.
	 */
	vk::Semaphore Release();

	/**
	 * Signal that this Semaphore will receive its signal from a "foreign" queue, usually the presentation engine.
	 */
	void SetForeignQueue();

	/**
	 * Signal that this Semaphore has been submitted for waiting.
	 */
	void SetPendingWait();

	/**
	 * Signal that this Semaphore has been submitted for signalling.
	 */
	void SignalExternal();

	/**
	 * Signal that this Semaphore has been waited on and is no longer signalled.
	 */
	void WaitExternal();

 private:
	explicit Semaphore(Device& device);
	Semaphore(Device& device, vk::Semaphore semaphore, bool signalled, bool owned, const std::string& debugName = "");
	Semaphore(
		Device& device, vk::Semaphore semaphore, uint64_t timelineValue, bool owned, const std::string& debugName = "");

	Device& _device;                 /**< The Device this Semaphore belongs to. */
	std::string _debugName;          /**< The name assigned to this Semaphore. */
	vk::Semaphore _semaphore;        /**< The Semaphore handle. */
	uint64_t _timelineValue = 0;     /**< The current timeline value of this Semaphore. */
	bool _owned             = false; /**< Specifies whether this class owns the Semaphore handle and should destroy it. */
	bool _isForeignQueue    = false;
	bool _pendingWait       = false; /**< Specifies whether this Semaphore has been submitted for waiting on. */
	vk::SemaphoreType _semaphoreType = vk::SemaphoreType::eBinary; /**< The type of the Semaphore handle. */
	bool _signalled                  = false; /**< Specifies whether this Semaphore has been submitted for signalling. */
};
}  // namespace Vulkan
}  // namespace Luna
