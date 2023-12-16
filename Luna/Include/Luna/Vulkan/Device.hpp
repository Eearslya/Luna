#pragma once

#include <Luna/Utility/SpinLock.hpp>
#include <Luna/Utility/TemporaryHashMap.hpp>
#include <Luna/Vulkan/Common.hpp>
#include <Luna/Vulkan/QueryPool.hpp>
#include <Luna/Vulkan/RenderPass.hpp>

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
	friend class Framebuffer;
	friend class Image;
	friend struct ImageDeleter;
	friend class ImageView;
	friend struct ImageViewDeleter;
	friend class PipelineLayout;
	friend class Program;
	friend class RenderPass;
	friend class QueryPool;
	friend class QueryResult;
	friend struct QueryResultDeleter;
	friend class Sampler;
	friend struct SamplerDeleter;
	friend class Semaphore;
	friend struct SemaphoreDeleter;
	friend class Shader;

 public:
	Device(Context& context);
	~Device() noexcept;

	[[nodiscard]] vk::Device GetDevice() const noexcept {
		return _device;
	}
	[[nodiscard]] const DeviceInfo& GetDeviceInfo() const noexcept {
		return _deviceInfo;
	}
	[[nodiscard]] uint32_t GetFrameIndex() const noexcept {
		return _currentFrameContext;
	}
	[[nodiscard]] uint32_t GetFramesInFlight() const noexcept {
		return _frameContexts.size();
	}
	[[nodiscard]] vk::Instance GetInstance() const noexcept {
		return _instance;
	}
	[[nodiscard]] const QueueInfo& GetQueueInfo() const noexcept {
		return _queueInfo;
	}

	vk::Format GetDefaultDepthFormat() const;
	vk::Format GetDefaultDepthStencilFormat() const;
	QueueType GetQueueType(CommandBufferType type) const;
	bool IsFormatSupported(vk::Format format, vk::FormatFeatureFlags features, vk::ImageTiling tiling) const;

	/* ==============================================
	** ===== Public Object Management Functions =====
	*  ============================================== */
	[[nodiscard]] BufferHandle CreateBuffer(const BufferCreateInfo& createInfo,
	                                        const void* initialData      = nullptr,
	                                        const std::string& debugName = "");
	[[nodiscard]] ImageHandle CreateImage(const ImageCreateInfo& createInfo,
	                                      const ImageInitialData* initialData = nullptr,
	                                      const std::string& debugName        = "");
	[[nodiscard]] SamplerHandle CreateSampler(const SamplerCreateInfo& samplerCI, const std::string& debugName = "");
	[[nodiscard]] const Sampler& GetStockSampler(StockSampler type) const;
	[[nodiscard]] RenderPassInfo GetSwapchainRenderPass(
		SwapchainRenderPassType type = SwapchainRenderPassType::ColorOnly) const noexcept;
	[[nodiscard]] ImageView& GetSwapchainView();
	[[nodiscard]] const ImageView& GetSwapchainView() const;
	[[nodiscard]] ImageView& GetSwapchainView(uint32_t index);
	[[nodiscard]] const ImageView& GetSwapchainView(uint32_t index) const;
	[[nodiscard]] TimestampReport GetTimestampReport(const std::string& name);
	void RegisterTimeInterval(QueryResultHandle start, QueryResultHandle end, const std::string& name);
	[[nodiscard]] DescriptorSetAllocator* RequestDescriptorSetAllocator(const DescriptorSetLayout& layout,
	                                                                    const vk::ShaderStageFlags* stagesForBindings);
	[[nodiscard]] const ImmutableSampler* RequestImmutableSampler(const SamplerCreateInfo& samplerCI);
	[[nodiscard]] Program* RequestProgram(const std::array<Shader*, ShaderStageCount>& shaders);
	[[nodiscard]] Program* RequestProgram(Shader* compute);
	[[nodiscard]] Program* RequestProgram(const std::vector<uint32_t>& compCode);
	[[nodiscard]] Program* RequestProgram(size_t compCodeSize, const void* compCode);
	[[nodiscard]] Program* RequestProgram(Shader* vertex, Shader* fragment);
	[[nodiscard]] Program* RequestProgram(const std::vector<uint32_t>& vertexCode,
	                                      const std::vector<uint32_t>& fragmentCode);
	[[nodiscard]] Program* RequestProgram(size_t vertexCodeSize,
	                                      const void* vertexCode,
	                                      size_t fragmentCodeSize,
	                                      const void* fragmentCode);
	[[nodiscard]] SemaphoreHandle RequestProxySemaphore();
	[[nodiscard]] SemaphoreHandle RequestSemaphore(const std::string& debugName = "");
	[[nodiscard]] Shader* RequestShader(Hash hash);
	[[nodiscard]] Shader* RequestShader(const std::vector<uint32_t>& code);
	[[nodiscard]] Shader* RequestShader(size_t codeSize, const void* code);
	[[nodiscard]] ImageHandle RequestTransientAttachment(const vk::Extent2D& extent,
	                                                     vk::Format format,
	                                                     uint32_t index                  = 0,
	                                                     vk::SampleCountFlagBits samples = vk::SampleCountFlagBits::e1,
	                                                     uint32_t arrayLayers            = 1);
	[[nodiscard]] QueryResultHandle WriteTimestamp(vk::CommandBuffer cmd, vk::PipelineStageFlags2 stages);

	/** Set the debug name for the given object. */
	void SetObjectName(vk::ObjectType type, uint64_t handle, const std::string& name);
	template <typename T>
	void SetObjectName(T object, const std::string& name) {
		SetObjectName(T::objectType, reinterpret_cast<uint64_t>(static_cast<typename T::NativeType>(object)), name);
	}

	/* ============================================
	** ===== Public Synchronization Functions =====
	*  ============================================ */
	void AddWaitSemaphore(CommandBufferType cmdType,
	                      SemaphoreHandle semaphore,
	                      vk::PipelineStageFlags2 stages,
	                      bool flush);
	SemaphoreHandle ConsumeReleaseSemaphore() noexcept;
	void EndFrame();
	void FlushFrame();

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

		struct Timestamp {
			QueryResultHandle Start;
			QueryResultHandle End;
			TimestampInterval* TimestampTag;
		};

		void Begin();
		void Trim();

		Device& Parent;
		const uint32_t FrameIndex;
		std::array<std::vector<CommandPool>, QueueTypeCount> CommandPools;

		QueryPool QueryPool;
		std::array<std::vector<CommandBufferHandle>, QueueTypeCount> Submissions;
		std::array<uint64_t, QueueTypeCount> TimelineValues;
		std::vector<Timestamp> TimestampIntervals;

		std::vector<VmaAllocation> AllocationsToFree;
		std::vector<VmaAllocation> AllocationsToUnmap;
		std::vector<vk::Buffer> BuffersToDestroy;
		std::vector<vk::Fence> FencesToAwait;
		std::vector<vk::Fence> FencesToRecycle;
		std::vector<vk::Framebuffer> FramebuffersToDestroy;
		std::vector<vk::Image> ImagesToDestroy;
		std::vector<vk::ImageView> ImageViewsToDestroy;
		std::vector<vk::Sampler> SamplersToDestroy;
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
	void DestroyFramebuffer(vk::Framebuffer framebuffer);
	void DestroyFramebufferNoLock(vk::Framebuffer framebuffer);
	void DestroyImage(vk::Image image);
	void DestroyImageNoLock(vk::Image image);
	void DestroyImageView(vk::ImageView imageView);
	void DestroyImageViewNoLock(vk::ImageView imageView);
	void DestroySampler(vk::Sampler sampler);
	void DestroySamplerNoLock(vk::Sampler sampler);
	void DestroySemaphore(vk::Semaphore semaphore);
	void DestroySemaphoreNoLock(vk::Semaphore semaphore);
	void FreeAllocation(VmaAllocation allocation, bool mapped);
	void FreeAllocationNoLock(VmaAllocation allocation, bool mapped);
	void FreeFence(vk::Fence fence);
	void FreeSemaphore(vk::Semaphore semaphore);
	TimestampInterval* GetTimestampTag(const std::string& name);
	void RegisterTimeIntervalNoLock(QueryResultHandle start, QueryResultHandle end, const std::string& name);
	void RecycleSemaphore(vk::Semaphore semaphore);
	void RecycleSemaphoreNoLock(vk::Semaphore semaphore);
	const Framebuffer& RequestFramebuffer(const RenderPassInfo& rpInfo);
	PipelineLayout* RequestPipelineLayout(const ProgramResourceLayout& resourceLayout);
	const RenderPass& RequestRenderPass(const RenderPassInfo& rpInfo, bool compatible = false);
	void ResetFence(vk::Fence fence, bool observedWait);
	void ResetFenceNoLock(vk::Fence fence, bool observedWait);
	[[nodiscard]] QueryResultHandle WriteTimestampNoLock(vk::CommandBuffer cmd, vk::PipelineStageFlags2 stages);

	/* =============================================
	** ===== Private Synchronization Functions =====
	*  ============================================= */
	void AddWaitSemaphoreNoLock(QueueType queueType,
	                            SemaphoreHandle semaphore,
	                            vk::PipelineStageFlags2 stages,
	                            bool flush);
	void EndFrameNoLock();
	void FlushFrameNoLock();
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
	double ConvertDeviceTimestampDelta(uint64_t startTicks, uint64_t endTicks) const;
	FrameContext& Frame();

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
		std::mutex FramebufferLock;
		std::mutex TransientAttachmentLock;
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
	VulkanObjectPool<QueryResult> _queryResultPool;
	VulkanObjectPool<Sampler> _samplerPool;
	VulkanObjectPool<Semaphore> _semaphorePool;

	SemaphoreHandle _swapchainAcquire;
	bool _swapchainAcquireConsumed = false;
	std::vector<ImageHandle> _swapchainImages;
	uint32_t _swapchainIndex = std::numeric_limits<uint32_t>::max();
	SemaphoreHandle _swapchainRelease;

	std::array<QueueData, QueueTypeCount> _queueData;

	VulkanCache<DescriptorSetAllocator> _descriptorSetAllocators;
	VulkanCache<ImmutableSampler> _immutableSamplers;
	VulkanCache<PipelineLayout> _pipelineLayouts;
	VulkanCache<Program> _programs;
	VulkanCache<RenderPass> _renderPasses;
	VulkanCache<Shader> _shaders;

	TemporaryHashMap<FramebufferNode, 8, false> _framebuffers;
	std::array<const ImmutableSampler*, StockSamplerCount> _stockSamplers;
	IntrusiveHashMap<TimestampInterval> _timestamps;
	TemporaryHashMap<TransientAttachmentNode, 8, false> _transientAttachments;
};
}  // namespace Vulkan
}  // namespace Luna
