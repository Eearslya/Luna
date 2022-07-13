#pragma once

#include <vk_mem_alloc.h>

#ifdef LUNA_VULKAN_MT
#	include <condition_variable>
#endif

#include "Common.hpp"

namespace Luna {
namespace Vulkan {
class Device : public IntrusivePtrEnabled<Device, std::default_delete<Device>, HandleCounter> {
 public:
	friend class Buffer;
	friend struct BufferDeleter;
	friend class Fence;
	friend struct FenceDeleter;
	friend class Semaphore;
	friend struct SemaphoreDeleter;

	Device(const Context& context);
	Device(const Device&)            = delete;
	Device& operator=(const Device&) = delete;
	~Device() noexcept;

	const vk::Device& GetDevice() const {
		return _device;
	}
	const ExtensionInfo& GetExtensionInfo() const {
		return _extensions;
	}

	BufferHandle CreateBuffer(const BufferCreateInfo& bufferCI, const void* initialData = nullptr);

	CommandBufferHandle RequestCommandBuffer(CommandBufferType type = CommandBufferType::Generic);
	CommandBufferHandle RequestCommandBufferForThread(uint32_t threadIndex,
	                                                  CommandBufferType type = CommandBufferType::Generic);

	uint64_t AllocateCookie();
	void AddWaitSemaphore(CommandBufferType cbType, SemaphoreHandle semaphore, vk::PipelineStageFlags stages, bool flush);
	void WaitIdle();

 private:
	struct FrameContext {
		FrameContext(Device& device, uint32_t index);
		FrameContext(const FrameContext&)            = delete;
		FrameContext& operator=(const FrameContext&) = delete;
		~FrameContext() noexcept;

		void Begin();

		Device& Parent;
		uint32_t Index;

		std::array<std::vector<std::unique_ptr<CommandPool>>, QueueTypeCount> CommandPools;
		std::vector<vk::Fence> FencesToAwait;
		std::array<std::vector<CommandBufferHandle>, QueueTypeCount> Submissions;
		std::array<vk::Semaphore, QueueTypeCount> TimelineSemaphores;
		std::array<uint64_t, QueueTypeCount> TimelineValues;

		std::vector<vk::Buffer> BuffersToDestroy;
		std::vector<vk::Fence> FencesToRecycle;
		std::vector<VmaAllocation> MemoryToFree;
		std::vector<vk::Semaphore> SemaphoresToDestroy;
		std::vector<vk::Semaphore> SemaphoresToRecycle;
	};

	struct QueueData {
		bool NeedsFence = false;
		vk::Semaphore TimelineSemaphore;
		uint64_t TimelineValue = 0;
		std::vector<SemaphoreHandle> WaitSemaphores;
		std::vector<vk::PipelineStageFlags> WaitStages;
	};

	// A small structure to keep track of a "fence". An internal fence can be represented by an actual fence, or by a
	// timeline semaphore, depending on available device extensions.
	struct InternalFence {
		vk::Fence Fence;
		vk::Semaphore TimelineSemaphore;
		uint64_t TimelineValue = 0;
	};

	vk::Fence AllocateFence();
	vk::Semaphore AllocateSemaphore();
	void CreateTimelineSemaphores();
	void DestroyTimelineSemaphores();
	FrameContext& Frame();
	QueueType GetQueueType(CommandBufferType cbType) const;
	void ReleaseFence(vk::Fence fence);
	void ReleaseSemaphore(vk::Semaphore semaphore);

	void DestroyBuffer(vk::Buffer buffer);
	void DestroySemaphore(vk::Semaphore semaphore);
	void FreeMemory(const VmaAllocation& allocation);
	void RecycleSemaphore(vk::Semaphore semaphore);
	void ResetFence(vk::Fence fence, bool observedWait);

	CommandBufferHandle RequestCommandBufferNoLock(uint32_t threadIndex, CommandBufferType type);

	void DestroyBufferNoLock(vk::Buffer buffer);
	void DestroySemaphoreNoLock(vk::Semaphore semaphore);
	void FreeMemoryNoLock(const VmaAllocation& allocation);
	void RecycleSemaphoreNoLock(vk::Semaphore semaphore);
	void ResetFenceNoLock(vk::Fence fence, bool observedWait);

	void AddWaitSemaphoreNoLock(QueueType queueType,
	                            SemaphoreHandle semaphore,
	                            vk::PipelineStageFlags stages,
	                            bool flush);
	void EndFrameNoLock();
	void FlushFrame(QueueType queueType);
	void SubmitNoLock(CommandBufferHandle cmd, FenceHandle* fence, std::vector<SemaphoreHandle>* semaphores);
	void SubmitQueue(QueueType queueType, InternalFence* submitFence, std::vector<SemaphoreHandle>* semaphores);
	void SubmitStaging(CommandBufferHandle& cmd, vk::BufferUsageFlags usage, bool flush);
	void WaitIdleNoLock();

	const ExtensionInfo _extensions;
	const vk::Instance _instance;
	const GPUInfo _gpuInfo;
	const QueueInfo _queues;
	const vk::PhysicalDevice _gpu;
	const vk::Device _device;

	uint32_t _currentFrameContext = 0;
	std::vector<std::unique_ptr<FrameContext>> _frameContexts;
	std::array<QueueData, QueueTypeCount> _queueData;

	VmaAllocator _allocator;
	std::vector<vk::Fence> _availableFences;
	std::vector<vk::Semaphore> _availableSemaphores;

#ifdef LUNA_VULKAN_MT
	std::atomic_uint64_t _cookie;
	struct {
		std::condition_variable Condition;
		uint32_t Counter = 0;
		std::mutex Mutex;
	} _lock;
#else
	uint64_t _cookie = 0;
	struct {
		uint32_t Counter = 0;
	} _lock;
#endif

	VulkanObjectPool<Buffer> _bufferPool;
	VulkanObjectPool<CommandBuffer> _commandBufferPool;
	VulkanObjectPool<Fence> _fencePool;
	VulkanObjectPool<Semaphore> _semaphorePool;
};
}  // namespace Vulkan
}  // namespace Luna
