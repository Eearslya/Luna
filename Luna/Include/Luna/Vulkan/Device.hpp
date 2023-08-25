#pragma once

#include <Luna/Utility/SpinLock.hpp>
#include <Luna/Vulkan/Common.hpp>

namespace Luna {
namespace Vulkan {
class Device : public VulkanObject<Device> {
	friend class Buffer;
	friend struct BufferDeleter;
	friend class CommandPool;
	friend class CommandBuffer;
	friend struct CommandBufferDeleter;
	friend class Cookie;
	friend class Fence;
	friend struct FenceDeleter;
	friend class Image;
	friend struct ImageDeleter;
	friend class ImageView;
	friend struct ImageViewDeleter;
	friend class Semaphore;
	friend struct SemaphoreDeleter;

 public:
	Device(Context& context);
	~Device() noexcept;

	[[nodiscard]] vk::Device GetDevice() const noexcept {
		return _device;
	}
	[[nodiscard]] const DeviceInfo& GetDeviceInfo() const noexcept {
		return _deviceInfo;
	}
	[[nodiscard]] vk::Instance GetInstance() const noexcept {
		return _instance;
	}
	[[nodiscard]] const QueueInfo& GetQueueInfo() const noexcept {
		return _queueInfo;
	}

	/* ==============================================
	** ===== Public Object Management Functions =====
	*  ============================================== */
	[[nodiscard]] BufferHandle CreateBuffer(const BufferCreateInfo& createInfo,
	                                        const void* initialData      = nullptr,
	                                        const std::string& debugName = "");

	[[nodiscard]] ImageHandle CreateImage(const ImageCreateInfo& createInfo,
	                                      const ImageInitialData* initialData = nullptr,
	                                      const std::string& debugName        = "");
	[[nodiscard]] SemaphoreHandle RequestSemaphore(const std::string& debugName = "");

	/** Set the debug name for the given object. */
	void SetObjectName(vk::ObjectType type, uint64_t handle, const std::string& name);
	template <typename T>
	void SetObjectName(T object, const std::string& name) {
		SetObjectName(T::objectType, reinterpret_cast<uint64_t>(static_cast<typename T::NativeType>(object)), name);
	}

	/* ============================================
	** ===== Public Synchronization Functions =====
	*  ============================================ */
	SemaphoreHandle ConsumeReleaseSemaphore() noexcept;

	void EndFrame();

	/** Advance to the next frame context. */
	void NextFrame();

	/** Request a new command buffer for the current thread. */
	[[nodiscard]] CommandBufferHandle RequestCommandBuffer(CommandBufferType type       = CommandBufferType::Generic,
	                                                       const std::string& debugName = "");

	/** Request a new command buffer for the specified thread. */
	[[nodiscard]] CommandBufferHandle RequestCommandBufferForThread(uint32_t threadIndex,
	                                                                CommandBufferType type = CommandBufferType::Generic,
	                                                                const std::string& debugName = "");

	void SetAcquireSemaphore(uint32_t imageIndex, SemaphoreHandle semaphore);

	void SetupSwapchain(const vk::Extent2D& extent,
	                    const vk::SurfaceFormatKHR& format,
	                    const std::vector<vk::Image>& images);

	[[nodiscard]] bool SwapchainAcquired() const;

	/**
	 * Submit a command buffer for execution.
	 *
	 * Optionally, provide a pointer to a FenceHandle object and one or more SemaphoreHandle objects.
	 * These objects will be created and overwritten with objects that will be signalled when the given command buffer has
	 * finished execution.
	 */
	void Submit(CommandBufferHandle& commandBuffer,
	            FenceHandle* fence                       = nullptr,
	            std::vector<SemaphoreHandle>* semaphores = nullptr);

	/** Wait for all submissions to complete and the device to be idle. */
	void WaitIdle();

 private:
	struct FrameContext {
		FrameContext(Device& parent, uint32_t frameIndex);
		FrameContext(const FrameContext&)            = delete;
		FrameContext& operator=(const FrameContext&) = delete;
		~FrameContext() noexcept;

		void Begin();
		void Trim();

		Device& Parent;
		const uint32_t FrameIndex;
		std::array<std::vector<CommandPool>, QueueTypeCount> CommandPools;

		std::array<std::vector<CommandBufferHandle>, QueueTypeCount> Submissions;
		std::array<uint64_t, QueueTypeCount> TimelineValues;

		std::vector<VmaAllocation> AllocationsToFree;
		std::vector<VmaAllocation> AllocationsToUnmap;
		std::vector<vk::Buffer> BuffersToDestroy;
		std::vector<vk::Fence> FencesToAwait;
		std::vector<vk::Fence> FencesToRecycle;
		std::vector<vk::Image> ImagesToDestroy;
		std::vector<vk::ImageView> ImageViewsToDestroy;
		std::vector<vk::Semaphore> SemaphoresToConsume;
		std::vector<vk::Semaphore> SemaphoresToDestroy;
		std::vector<vk::Semaphore> SemaphoresToRecycle;
	};

	/** Structure to represent an internal "fence", which depending on device features, may be a normal VkFence or a
	 * Timeline Semaphore. */
	struct InternalFence {
		vk::Fence Fence;
		vk::Semaphore TimelineSemaphore;
		uint64_t TimelineValue;
	};

	struct QueueData {
		bool NeedsFence = false;
		vk::Semaphore TimelineSemaphore;
		uint64_t TimelineValue = 0;
		std::vector<SemaphoreHandle> WaitSemaphores;
		std::vector<vk::PipelineStageFlags2> WaitStages;
	};

	/* ===============================================
	** ===== Private Object Management Functions =====
	*  =============================================== */
	vk::Fence AllocateFence();
	vk::Semaphore AllocateSemaphore(const std::string& debugName = "");
	void ConsumeSemaphore(vk::Semaphore semaphore);
	void ConsumeSemaphoreNoLock(vk::Semaphore semaphore);
	void DestroyBuffer(vk::Buffer buffer);
	void DestroyBufferNoLock(vk::Buffer buffer);
	void DestroyImage(vk::Image image);
	void DestroyImageNoLock(vk::Image image);
	void DestroyImageView(vk::ImageView imageView);
	void DestroyImageViewNoLock(vk::ImageView imageView);
	void DestroySemaphore(vk::Semaphore semaphore);
	void DestroySemaphoreNoLock(vk::Semaphore semaphore);
	void FreeAllocation(VmaAllocation allocation, bool mapped);
	void FreeAllocationNoLock(VmaAllocation allocation, bool mapped);
	void FreeFence(vk::Fence fence);
	void FreeSemaphore(vk::Semaphore semaphore);
	void RecycleSemaphore(vk::Semaphore semaphore);
	void RecycleSemaphoreNoLock(vk::Semaphore semaphore);
	void ResetFence(vk::Fence fence, bool observedWait);
	void ResetFenceNoLock(vk::Fence fence, bool observedWait);

	/* =============================================
	** ===== Private Synchronization Functions =====
	*  ============================================= */
	void AddWaitSemaphoreNoLock(QueueType queueType,
	                            SemaphoreHandle semaphore,
	                            vk::PipelineStageFlags2 stages,
	                            bool flush);
	void EndFrameNoLock();
	void FlushQueue(QueueType queueType);
	CommandBufferHandle RequestCommandBufferNoLock(uint32_t threadIndex,
	                                               CommandBufferType type,
	                                               const std::string& debugName);
	void SubmitNoLock(CommandBufferHandle commandBuffer, FenceHandle* fence, std::vector<SemaphoreHandle>* semaphores);
	void SubmitQueue(QueueType queueType, InternalFence* signalFence, std::vector<SemaphoreHandle>* semaphores);
	void SubmitStaging(CommandBufferHandle commandBuffer, vk::PipelineStageFlags2 stages, bool flush);
	void SubmitStagingNoLock(CommandBufferHandle commandBuffer, vk::PipelineStageFlags2 stages, bool flush);
	void WaitIdleNoLock();

	/* ====================================
	** ===== Private Helper Functions =====
	*  ==================================== */
	uint64_t AllocateCookie();
	void CreateFrameContexts(uint32_t count);
	FrameContext& Frame();
	QueueType GetQueueType(CommandBufferType type) const;

	const Extensions& _extensions;
	const vk::Instance& _instance;
	const DeviceInfo& _deviceInfo;
	const QueueInfo& _queueInfo;
	const vk::Device& _device;

	struct {
		std::condition_variable Condition;
		uint32_t Counter = 0;
		std::mutex Lock;
		std::mutex MemoryLock;
		RWSpinLock ReadOnlyCache;
	} _lock;
	std::atomic_uint64_t _nextCookie;

	uint32_t _currentFrameContext = 0;
	std::vector<std::unique_ptr<FrameContext>> _frameContexts;

	VmaAllocator _allocator = nullptr;
	std::vector<vk::Fence> _availableFences;
	std::vector<vk::Semaphore> _availableSemaphores;

	VulkanObjectPool<Buffer> _bufferPool;
	VulkanObjectPool<CommandBuffer> _commandBufferPool;
	VulkanObjectPool<Fence> _fencePool;
	VulkanObjectPool<Image> _imagePool;
	VulkanObjectPool<ImageView> _imageViewPool;
	VulkanObjectPool<Semaphore> _semaphorePool;

	SemaphoreHandle _swapchainAcquire;
	bool _swapchainAcquireConsumed = false;
	std::vector<ImageHandle> _swapchainImages;
	uint32_t _swapchainIndex = std::numeric_limits<uint32_t>::max();
	SemaphoreHandle _swapchainRelease;

	std::array<QueueData, QueueTypeCount> _queueData;
};
}  // namespace Vulkan
}  // namespace Luna
