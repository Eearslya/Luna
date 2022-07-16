#pragma once

#include <vk_mem_alloc.h>

#ifdef LUNA_VULKAN_MT
#	include <condition_variable>
#endif

#include "Common.hpp"

namespace Luna {
namespace Vulkan {
struct ImageInitialData {
	const void* Data     = nullptr;
	uint32_t RowLength   = 0;
	uint32_t ImageHeight = 0;
};

class Device : public IntrusivePtrEnabled<Device, std::default_delete<Device>, HandleCounter> {
 public:
	friend class Buffer;
	friend struct BufferDeleter;
	friend class CommandBuffer;
	friend struct CommandBufferDeleter;
	friend class Fence;
	friend struct FenceDeleter;
	friend class FramebufferAllocator;
	friend class Image;
	friend struct ImageDeleter;
	friend class ImageView;
	friend struct ImageViewDeleter;
	friend class RenderPass;
	friend class Semaphore;
	friend struct SemaphoreDeleter;
	friend class WSI;

	Device(const Context& context);
	Device(const Device&) = delete;
	Device& operator=(const Device&) = delete;
	~Device() noexcept;

	const vk::Device& GetDevice() const {
		return _device;
	}
	const ExtensionInfo& GetExtensionInfo() const {
		return _extensions;
	}
	uint32_t GetFrameIndex() const {
		return _currentFrameContext;
	}
	const GPUInfo& GetGPUInfo() const {
		return _gpuInfo;
	}

	BufferHandle CreateBuffer(const BufferCreateInfo& bufferCI, const void* initialData = nullptr);
	ImageHandle CreateImage(const ImageCreateInfo& imageCI, const ImageInitialData* initialData = nullptr);
	vk::Format GetDefaultDepthFormat() const;
	vk::Format GetDefaultDepthStencilFormat() const;
	RenderPassInfo GetStockRenderPass(StockRenderPass type = StockRenderPass::ColorOnly) const;
	bool ImageFormatSupported(vk::Format format, vk::FormatFeatureFlags features, vk::ImageTiling tiling) const;
	DescriptorSetAllocator* RequestDescriptorSetAllocator(const DescriptorSetLayout& layout,
	                                                      const uint32_t* stagesForBindings);
	PipelineLayout* RequestPipelineLayout(const ProgramResourceLayout& layout);
	Program* RequestProgram(size_t compCodeSize, const void* compCode);
	Program* RequestProgram(size_t vertCodeSize, const void* vertCode, size_t fragCodeSize, const void* fragCode);
	Program* RequestProgram(Shader* compute);
	Program* RequestProgram(const std::string& computeGlsl);
	Program* RequestProgram(Shader* vertex, Shader* fragment);
	Program* RequestProgram(const std::string& vertexGlsl, const std::string& fragmentGlsl);
	Sampler* RequestSampler(const SamplerCreateInfo& createInfo);
	Sampler* RequestSampler(StockSampler type);
	SemaphoreHandle RequestSemaphore(const std::string& debugName = "");
	Shader* RequestShader(size_t codeSize, const void* code);
	Shader* RequestShader(vk::ShaderStageFlagBits stage, const std::string& glsl);
	ImageHandle RequestTransientAttachment(const vk::Extent2D& extent,
	                                       vk::Format format,
	                                       uint32_t index                  = 0,
	                                       vk::SampleCountFlagBits samples = vk::SampleCountFlagBits::e1,
	                                       uint32_t layers                 = 1) const;

	CommandBufferHandle RequestCommandBuffer(CommandBufferType type = CommandBufferType::Generic);
	CommandBufferHandle RequestCommandBufferForThread(uint32_t threadIndex,
	                                                  CommandBufferType type = CommandBufferType::Generic);

	uint64_t AllocateCookie();
	void AddWaitSemaphore(CommandBufferType cbType, SemaphoreHandle semaphore, vk::PipelineStageFlags stages, bool flush);
	void EndFrame();
	void NextFrame();
	void Submit(CommandBufferHandle cmd,
	            FenceHandle* fence                       = nullptr,
	            std::vector<SemaphoreHandle>* semaphores = nullptr);
	void WaitIdle();

 private:
	struct FrameContext {
		FrameContext(Device& device, uint32_t index);
		FrameContext(const FrameContext&) = delete;
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
		std::vector<vk::Image> ImagesToDestroy;
		std::vector<vk::ImageView> ImageViewsToDestroy;
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
	void CreateStockSamplers();
	void CreateTimelineSemaphores();
	SemaphoreHandle ConsumeReleaseSemaphore();
	void DestroyTimelineSemaphores();
	FrameContext& Frame();
	QueueType GetQueueType(CommandBufferType cbType) const;
	void ReleaseFence(vk::Fence fence);
	void ReleaseSemaphore(vk::Semaphore semaphore);
	void SetAcquireSemaphore(uint32_t imageIndex, SemaphoreHandle& semaphore);
	void SetupSwapchain(WSI& wsi);

	void DestroyBuffer(vk::Buffer buffer);
	void DestroyImage(vk::Image image);
	void DestroyImageView(vk::ImageView view);
	void DestroySemaphore(vk::Semaphore semaphore);
	void FreeMemory(const VmaAllocation& allocation);
	void RecycleSemaphore(vk::Semaphore semaphore);
	Framebuffer& RequestFramebuffer(const RenderPassInfo& info);
	void ResetFence(vk::Fence fence, bool observedWait);

	CommandBufferHandle RequestCommandBufferNoLock(uint32_t threadIndex, CommandBufferType type);
	RenderPass& RequestRenderPass(const RenderPassInfo& info, bool compatible = false);

	void DestroyBufferNoLock(vk::Buffer buffer);
	void DestroyImageNoLock(vk::Image image);
	void DestroyImageViewNoLock(vk::ImageView view);
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
	std::unique_ptr<FramebufferAllocator> _framebufferAllocator;
	std::unique_ptr<ShaderCompiler> _shaderCompiler;
	std::unique_ptr<TransientAttachmentAllocator> _transientAttachmentAllocator;

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

	SemaphoreHandle _swapchainAcquire;
	bool _swapchainAcquireConsumed = false;
	std::vector<ImageHandle> _swapchainImages;
	uint32_t _swapchainIndex;
	SemaphoreHandle _swapchainRelease;

	VulkanObjectPool<Buffer> _bufferPool;
	VulkanObjectPool<CommandBuffer> _commandBufferPool;
	VulkanObjectPool<Fence> _fencePool;
	VulkanObjectPool<Image> _imagePool;
	VulkanObjectPool<ImageView> _imageViewPool;
	VulkanObjectPool<Semaphore> _semaphorePool;

	VulkanCache<DescriptorSetAllocator> _descriptorSetAllocators;
	VulkanCache<PipelineLayout> _pipelineLayouts;
	VulkanCache<Program> _programs;
	VulkanCache<RenderPass> _renderPasses;
	VulkanCache<Sampler> _samplers;
	VulkanCache<Shader> _shaders;

	std::array<Sampler*, StockSamplerCount> _stockSamplers;
};
}  // namespace Vulkan
}  // namespace Luna
