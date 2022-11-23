#pragma once

#include <Luna/Vulkan/Common.hpp>
#include <atomic>

namespace Luna {
namespace Vulkan {
class Device : public IntrusivePtrEnabled<Device> {
	friend class CommandBuffer;
	friend struct CommandBufferDeleter;
	friend class Cookie;
	friend class Fence;
	friend struct FenceDeleter;
	friend class Image;
	friend struct ImageDeleter;
	friend class ImageView;
	friend struct ImageViewDeleter;
	friend class QueryPool;
	friend class QueryPoolResult;
	friend struct QueryPoolResultDeleter;
	friend class Semaphore;
	friend struct SemaphoreDeleter;
	friend class WSI;

 public:
	Device(Context& context);
	~Device() noexcept;

	vk::Device GetDevice() const {
		return _device;
	}
	const DeviceInfo& GetDeviceInfo() const {
		return _deviceInfo;
	}

	void EndFrame();
	void NextFrame();
	CommandBufferHandle RequestCommandBuffer(CommandBufferType type = CommandBufferType::Generic);
	CommandBufferHandle RequestCommandBufferForThread(uint32_t threadIndex,
	                                                  CommandBufferType type = CommandBufferType::Generic);
	CommandBufferHandle RequestProfiledCommandBuffer(CommandBufferType type = CommandBufferType::Generic);
	CommandBufferHandle RequestProfiledCommandBufferForThread(uint32_t threadIndex,
	                                                          CommandBufferType type = CommandBufferType::Generic);
	void Submit(CommandBufferHandle& cmd,
	            FenceHandle* fence                       = nullptr,
	            std::vector<SemaphoreHandle>* semaphores = nullptr);

	void WaitIdle();

	ImageHandle CreateImage(const ImageCreateInfo& imageCI, const ImageInitialData* initial = nullptr);
	ImageHandle CreateImageFromStagingBuffer(const ImageCreateInfo& imageCI, const ImageInitialBuffer* buffer);
	ImageViewHandle CreateImageView(const ImageViewCreateInfo& viewCI);
	SemaphoreHandle RequestSemaphore();

 private:
	struct FrameContext {
		FrameContext(Device& device, uint32_t frameIndex);
		~FrameContext() noexcept;

		void Begin();

		Device& Parent;
		const uint32_t FrameIndex;

		std::array<std::vector<std::unique_ptr<CommandPool>>, QueueTypeCount> CommandPools;
		std::array<std::vector<CommandBufferHandle>, QueueTypeCount> Submissions;
		std::array<uint64_t, QueueTypeCount> TimelineValues;

		std::vector<VmaAllocation> AllocationsToFree;
		std::vector<vk::Fence> FencesToAwait;
		std::vector<vk::Fence> FencesToRecycle;
		std::vector<vk::Image> ImagesToDestroy;
		std::vector<vk::ImageView> ImageViewsToDestroy;
		std::vector<vk::Semaphore> SemaphoresToDestroy;
		std::vector<vk::Semaphore> SemaphoresToRecycle;
	};

	struct InternalFence {
		vk::Fence Fence;
		vk::Semaphore Timeline;
		uint64_t TimelineValue = 0;
	};

	struct QueueData {
		bool NeedsFence = false;
		PerformanceQueryPoolHandle QueryPool;
		vk::Semaphore TimelineSemaphore;
		uint64_t TimelineValue = 0;
		std::vector<SemaphoreHandle> WaitSemaphores;
		std::vector<vk::PipelineStageFlags> WaitStages;
	};

	uint64_t AllocateCookie();
	vk::Fence AllocateFence();
	vk::Semaphore AllocateSemaphore();
	SemaphoreHandle ConsumeReleaseSemaphore();
	void CreateFrameContexts(uint32_t count);
	void CreateTimelineSemaphores();
	void DestroyTimelineSemaphores();
	QueueType GetQueueType(CommandBufferType cmdType) const;
	FrameContext& Frame();
	void ReleaseFence(vk::Fence fence);
	void ReleaseSemaphore(vk::Semaphore semaphore);
	void SetAcquireSemaphore(uint32_t imageIndex, SemaphoreHandle& semaphore);
	void SetupSwapchain(WSI& wsi);

	void EndFrameNoLock();
	void FlushFrame(QueueType queueType);
	CommandBufferHandle RequestCommandBufferNoLock(uint32_t threadIndex, CommandBufferType type, bool profiled);
	void SubmitNoLock(CommandBufferHandle cmd, FenceHandle* fence, std::vector<SemaphoreHandle>* semaphores);
	void SubmitQueue(QueueType queueType, InternalFence* submitFence, std::vector<SemaphoreHandle>* semaphores);
	void SubmitStaging(CommandBufferHandle& cmd, vk::BufferUsageFlags usage, bool flush);
	void WaitIdleNoLock();

	void DestroyImage(vk::Image image);
	void DestroyImageNoLock(vk::Image image);
	void DestroyImageView(vk::ImageView view);
	void DestroyImageViewNoLock(vk::ImageView view);
	void DestroySemaphore(vk::Semaphore semaphore);
	void DestroySemaphoreNoLock(vk::Semaphore semaphore);
	void FreeAllocation(const VmaAllocation& allocation);
	void FreeAllocationNoLock(const VmaAllocation& allocation);
	void RecycleSemaphore(vk::Semaphore semaphore);
	void RecycleSemaphoreNoLock(vk::Semaphore semaphore);
	void ResetFence(vk::Fence fence, bool observedWait);
	void ResetFenceNoLock(vk::Fence fence, bool observedWait);

	Extensions _extensions;
	vk::Instance _instance;
	DeviceInfo _deviceInfo;
	QueueInfo _queueInfo;
	vk::Device _device;

	uint32_t _currentFrameContext = 0;
	std::vector<std::unique_ptr<FrameContext>> _frameContexts;
	std::array<QueueData, QueueTypeCount> _queueData;

	VmaAllocator _allocator;
	std::vector<vk::Fence> _availableFences;
	std::vector<vk::Semaphore> _availableSemaphores;

#ifdef LUNA_VULKAN_MT
	std::atomic_uint64_t _nextCookie;
	struct {
		std::condition_variable Condition;
		uint32_t Counter = 0;
		std::mutex Mutex;
	} _lock;
#else
	uint64_t _nextCookie = 0;
	struct {
		uint32_t Counter = 0;
	} _lock;
#endif

	SemaphoreHandle _swapchainAcquire;
	bool _swapchainAcquireConsumed = false;
	std::vector<ImageHandle> _swapchainImages;
	uint32_t _swapchainIndex = std::numeric_limits<uint32_t>::max();
	SemaphoreHandle _swapchainRelease;

	VulkanObjectPool<CommandBuffer> _commandBufferPool;
	VulkanObjectPool<Fence> _fencePool;
	VulkanObjectPool<Image> _imagePool;
	VulkanObjectPool<ImageView> _imageViewPool;
	VulkanObjectPool<QueryPoolResult> _queryPoolResultPool;
	VulkanObjectPool<Semaphore> _semaphorePool;
};
}  // namespace Vulkan
}  // namespace Luna
