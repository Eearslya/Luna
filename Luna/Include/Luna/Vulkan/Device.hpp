#pragma once

#include <Luna/Vulkan/Common.hpp>

namespace Luna {
namespace Vulkan {
class Context;

class Device final : NonCopyable {
 public:
	Device(const Context& context);
	~Device() noexcept;

	// Access to device handles and information.
	const vk::Device& GetDevice() const {
		return _device;
	}
	const ExtensionInfo& GetExtensionInfo() const {
		return _extensions;
	}
	const vk::PhysicalDevice& GetGPU() const {
		return _gpu;
	}
	const GPUInfo& GetGPUInfo() const {
		return _gpuInfo;
	}
	const vk::Instance& GetInstance() const {
		return _instance;
	}
	const QueueInfo& GetQueueInfo() const {
		return _queues;
	}
	const vk::SurfaceKHR& GetSurface() const {
		return _surface;
	}

	// Frame management.
	void NextFrame();
	CommandBufferHandle RequestCommandBuffer(CommandBufferType type = CommandBufferType::Generic);
	void Submit(CommandBufferHandle& cmd, FenceHandle* fence = nullptr);

	// General functionality.
	void WaitIdle();

	// Internal functions for other Vulkan classes.
	uint64_t AllocateCookie(Badge<Cookie>);
	void RecycleSemaphore(Badge<SemaphoreDeleter>, Semaphore* semaphore);
	void ReleaseCommandBuffer(Badge<CommandBufferDeleter>, CommandBuffer* cmdBuf);
	void ResetFence(Badge<FenceDeleter>, Fence* fence);

 private:
	// A FrameContext contains all of the information needed to complete and clean up after a frame of work.
	struct FrameContext {
		FrameContext(Device& device, uint32_t frameIndex);
		~FrameContext() noexcept;

		void Begin();
		void TrimCommandPools();

		Device& Parent;
		uint32_t FrameIndex;

		std::array<std::vector<std::unique_ptr<CommandPool>>, QueueTypeCount> CommandPools;
		std::array<std::vector<CommandBufferHandle>, QueueTypeCount> Submissions;

		std::vector<Fence*> FencesToRecycle;
		std::vector<Semaphore*> SemaphoresToDestroy;
		std::vector<Semaphore*> SemaphoresToRecycle;
	};

	// A small structure to keep track of a "fence". An internal fence can be represented by an actual fence, or by a
	// timeline semaphore, depending on available device extensions.
	struct InternalFence {
		vk::Fence Fence;
		vk::Semaphore TimelineSemaphore;
		uint64_t TimelineValue = 0;
	};

	struct QueueData {
		std::vector<SemaphoreHandle> WaitSemaphores;
		std::vector<vk::PipelineStageFlags> WaitStages;
		bool NeedsFence = false;
		vk::Semaphore TimelineSemaphore;
		uint64_t TimelineValue = 0;
	};

	// Frame management.
	FrameContext& Frame();
	CommandBufferHandle RequestCommandBufferNoLock(CommandBufferType type, uint32_t threadIndex);
	void SubmitNoLock(CommandBufferHandle cmd, FenceHandle* fence);
	void SubmitQueue(QueueType queueType, InternalFence* submitFence);

	// General functionality.
	QueueType GetQueueType(CommandBufferType bufferType) const;
	void WaitIdleNoLock();

	// Internal setup and cleanup.
	void CreateFrameContexts(uint32_t count);
	void CreateTimelineSemaphores();
	void DestroyTimelineSemaphores();
	void ReleaseFence(vk::Fence fence);
	void ReleaseSemaphore(vk::Semaphore semaphore);
	vk::Fence RequestFence();
	vk::Semaphore RequestSemaphore();

	// All of our Vulkan information/objects inherited from Context.
	const ExtensionInfo& _extensions;
	const vk::Instance& _instance;
	const vk::SurfaceKHR& _surface;
	const GPUInfo& _gpuInfo;
	const QueueInfo& _queues;
	const vk::PhysicalDevice& _gpu;
	const vk::Device& _device;

	// Multithreading synchronization objects.
	std::vector<vk::Fence> _availableFences;
	std::vector<vk::Semaphore> _availableSemaphores;
#ifdef LUNA_VULKAN_MT
	std::mutex _mutex;
	std::atomic_uint64_t _nextCookie;
	std::condition_variable _pendingCommandBuffersCondition;
#else
	uint64_t _nextCookie = 0;
#endif
	uint32_t _pendingCommandBuffers = 0;
	std::array<QueueData, QueueTypeCount> _queueData;

	// Frame contexts.
	uint32_t _currentFrameContext = 0;
	std::vector<std::unique_ptr<FrameContext>> _frameContexts;

	// Vulkan object pools.
	VulkanObjectPool<CommandBuffer> _commandBufferPool;
	VulkanObjectPool<Fence> _fencePool;
	VulkanObjectPool<Semaphore> _semaphorePool;
};
}  // namespace Vulkan
}  // namespace Luna
