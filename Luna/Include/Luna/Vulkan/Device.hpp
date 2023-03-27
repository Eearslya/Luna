#pragma once

#include <Luna/Vulkan/BufferPool.hpp>
#include <Luna/Vulkan/Common.hpp>
#include <Luna/Vulkan/Tracing.hpp>
#include <atomic>

namespace Luna {
namespace Vulkan {
class Device : public IntrusivePtrEnabled<Device> {
	friend class BindlessDescriptorPool;
	friend struct BindlessDescriptorPoolDeleter;
	friend class Buffer;
	friend struct BufferDeleter;
	friend class CommandBuffer;
	friend struct CommandBufferDeleter;
	friend class CommandPool;
	friend class Cookie;
	friend class Fence;
	friend struct FenceDeleter;
	friend class Framebuffer;
	friend class FramebufferAllocator;
	friend class Image;
	friend struct ImageDeleter;
	friend class ImageView;
	friend struct ImageViewDeleter;
	friend class Sampler;
	friend class Semaphore;
	friend struct SemaphoreDeleter;
	friend class TransientAttachmentAllocator;
	friend class WSI;

 public:
	Device(Context& context);
	Device(const Device&)         = delete;
	void operator=(const Device&) = delete;
	~Device() noexcept;

	vk::Device GetDevice() const {
		return _device;
	}
	const DeviceInfo& GetDeviceInfo() const {
		return _deviceInfo;
	}
	uint32_t GetFrameIndex() const {
		return _currentFrameContext;
	}
	uint32_t GetFramesInFlight() const {
		return _frameContexts.size();
	}
	ShaderCompiler& GetShaderCompiler() {
		return *_shaderCompiler;
	}
	ShaderManager& GetShaderManager() {
		return *_shaderManager;
	}

	vk::Format GetDefaultDepthFormat() const;
	vk::Format GetDefaultDepthStencilFormat() const;
	vk::ImageViewType GetImageViewType(const ImageCreateInfo& imageCI, const ImageViewCreateInfo* viewCI) const;
	QueueType GetQueueType(CommandBufferType cmdType) const;
	bool IsFormatSupported(vk::Format format, vk::FormatFeatureFlags features, vk::ImageTiling tiling) const;

	void AddWaitSemaphore(CommandBufferType cbType,
	                      SemaphoreHandle semaphore,
	                      vk::PipelineStageFlags2 stages,
	                      bool flush);
	void EndFrame();
	void FlushFrame();
	void NextFrame();
	CommandBufferHandle RequestCommandBuffer(CommandBufferType type = CommandBufferType::Generic);
	CommandBufferHandle RequestCommandBufferForThread(uint32_t threadIndex,
	                                                  CommandBufferType type = CommandBufferType::Generic);
	void Submit(CommandBufferHandle& cmd,
	            FenceHandle* fence                       = nullptr,
	            std::vector<SemaphoreHandle>* semaphores = nullptr);
	void WaitIdle();

	BindlessDescriptorPoolHandle CreateBindlessDescriptorPool(uint32_t setCount, uint32_t descriptorCount);
	BufferHandle CreateBuffer(const BufferCreateInfo& bufferCI, const void* initial = nullptr);
	ImageHandle CreateImage(const ImageCreateInfo& imageCI, const ImageInitialData* initial = nullptr);
	ImageHandle CreateImageFromStagingBuffer(const ImageCreateInfo& imageCI, const ImageInitialBuffer* buffer);
	ImageInitialBuffer CreateImageStagingBuffer(const ImageCreateInfo& imageCI, const ImageInitialData* initial);
	ImageInitialBuffer CreateImageStagingBuffer(const TextureFormatLayout& layout);
	ImageViewHandle CreateImageView(const ImageViewCreateInfo& viewCI);
	RenderPassInfo GetSwapchainRenderPass(SwapchainRenderPassType type = SwapchainRenderPassType::ColorOnly);
	ImageView& GetSwapchainView();
	ImageView& GetSwapchainView(uint32_t index);
	ImageHandle GetTransientAttachment(const vk::Extent2D& extent,
	                                   vk::Format format,
	                                   uint32_t index                  = 0,
	                                   vk::SampleCountFlagBits samples = vk::SampleCountFlagBits::e1,
	                                   uint32_t arrayLayers            = 1);
	DescriptorSetAllocator* RequestDescriptorSetAllocator(const DescriptorSetLayout& layout,
	                                                      const uint32_t* stagesForBindings);
	Program* RequestProgram(size_t compCodeSize, const void* compCode);
	Program* RequestProgram(size_t vertCodeSize, const void* vertCode, size_t fragCodeSize, const void* fragCode);
	Program* RequestProgram(Shader* compute);
	Program* RequestProgram(const std::string& computeGlsl);
	Program* RequestProgram(Shader* vertex, Shader* fragment);
	Program* RequestProgram(const std::string& vertexGlsl, const std::string& fragmentGlsl);
	Sampler* RequestSampler(const SamplerCreateInfo& createInfo);
	Sampler* RequestSampler(StockSampler type);
	Shader* RequestShader(size_t codeSize, const void* code);
	Shader* RequestShader(vk::ShaderStageFlagBits stage, const std::string& glsl);
	Shader* RequestShader(Hash hash);
	PipelineLayout* RequestPipelineLayout(const ProgramResourceLayout& layout);
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
		std::vector<vk::Buffer> BuffersToDestroy;
		std::vector<vk::DescriptorPool> DescriptorPoolsToDestroy;
		std::vector<vk::Fence> FencesToAwait;
		std::vector<vk::Fence> FencesToRecycle;
		std::vector<vk::Framebuffer> FramebuffersToDestroy;
		std::vector<vk::Image> ImagesToDestroy;
		std::vector<vk::ImageView> ImageViewsToDestroy;
		std::vector<vk::Semaphore> SemaphoresToConsume;
		std::vector<vk::Semaphore> SemaphoresToDestroy;
		std::vector<vk::Semaphore> SemaphoresToRecycle;

		std::vector<BufferBlock> IndexBlocks;
		std::vector<BufferBlock> UniformBlocks;
		std::vector<BufferBlock> VertexBlocks;
	};

	struct ImageManager {
	 public:
		ImageManager(Device& device);
		ImageManager(const ImageManager&)            = delete;
		ImageManager(ImageManager&&)                 = delete;
		ImageManager& operator=(const ImageManager&) = delete;
		ImageManager& operator=(ImageManager&&)      = delete;
		~ImageManager() noexcept;

		bool CreateDefaultViews(const ImageCreateInfo& imageCI,
		                        const vk::ImageViewCreateInfo* viewInfo,
		                        bool createUnormSrgbViews     = false,
		                        const vk::Format* viewFormats = nullptr);

		Device& Parent;
		vk::Image Image;
		VmaAllocation Allocation;
		vk::ImageView ImageView;
		vk::ImageView DepthView;
		vk::ImageView StencilView;
		vk::ImageView UnormView;
		vk::ImageView SrgbView;
		vk::ImageViewType DefaultViewType;
		std::vector<vk::ImageView> RenderTargetViews;
		bool Owned = true;

	 private:
		bool CreateAltViews(const ImageCreateInfo& imageCI, const vk::ImageViewCreateInfo& viewCI);
		bool CreateDefaultView(const vk::ImageViewCreateInfo& viewCI);
		bool CreateRenderTargetViews(const ImageCreateInfo& imageCI, const vk::ImageViewCreateInfo& viewCI);
	};

	struct InternalFence {
		vk::Fence Fence;
		vk::Semaphore Timeline;
		uint64_t TimelineValue = 0;
	};

	struct QueueData {
		bool NeedsFence = false;
		vk::Semaphore TimelineSemaphore;
		uint64_t TimelineValue = 0;
		std::vector<SemaphoreHandle> WaitSemaphores;
		std::vector<vk::PipelineStageFlags2> WaitStages;

		TracyVkCtx TracingContext = nullptr;
	};

	void AddWaitSemaphoreNoLock(QueueType queueType,
	                            SemaphoreHandle semaphore,
	                            vk::PipelineStageFlags2 stages,
	                            bool flush);
	uint64_t AllocateCookie();
	vk::Fence AllocateFence();
	vk::Semaphore AllocateSemaphore();
	void CreateFrameContexts(uint32_t count);
	void CreateStockSamplers();
	void CreateTimelineSemaphores();
	void CreateTracingContexts();
	SemaphoreHandle ConsumeReleaseSemaphore();
	void DestroyTimelineSemaphores();
	void DestroyTracingContexts();
	FrameContext& Frame();
	void ReleaseFence(vk::Fence fence);
	void ReleaseSemaphore(vk::Semaphore semaphore);
	const Framebuffer& RequestFramebuffer(const RenderPassInfo& rpInfo);
	const RenderPass& RequestRenderPass(const RenderPassInfo& rpInfo, bool compatible = false);
	void SetAcquireSemaphore(uint32_t imageIndex, SemaphoreHandle& semaphore);
	void SetupSwapchain(WSI& wsi);

	void EndFrameNoLock();
	void FlushFrame(QueueType queueType);
	void FlushFrameNoLock();
	CommandBufferHandle RequestCommandBufferNoLock(uint32_t threadIndex, CommandBufferType type);
	void SubmitNoLock(CommandBufferHandle cmd, FenceHandle* fence, std::vector<SemaphoreHandle>* semaphores);
	void SubmitQueue(QueueType queueType, InternalFence* submitFence, std::vector<SemaphoreHandle>* semaphores);
	void SubmitStaging(CommandBufferHandle& cmd, bool flush);
	void SyncBufferBlocks();
	void WaitIdleNoLock();

	void RequestBlock(BufferBlock& block,
	                  vk::DeviceSize size,
	                  BufferPool& pool,
	                  std::vector<BufferBlock>& copies,
	                  std::vector<BufferBlock>& recycles);
	void RequestIndexBlock(BufferBlock& block, vk::DeviceSize size);
	void RequestIndexBlockNoLock(BufferBlock& block, vk::DeviceSize size);
	void RequestUniformBlock(BufferBlock& block, vk::DeviceSize size);
	void RequestUniformBlockNoLock(BufferBlock& block, vk::DeviceSize size);
	void RequestVertexBlock(BufferBlock& block, vk::DeviceSize size);
	void RequestVertexBlockNoLock(BufferBlock& block, vk::DeviceSize size);

	void ConsumeSemaphore(vk::Semaphore semaphore);
	void ConsumeSemaphoreNoLock(vk::Semaphore semaphore);
	void DestroyBuffer(vk::Buffer buffer);
	void DestroyBufferNoLock(vk::Buffer buffer);
	void DestroyDescriptorPool(vk::DescriptorPool pool);
	void DestroyDescriptorPoolNoLock(vk::DescriptorPool pool);
	void DestroyFramebuffer(vk::Framebuffer framebuffer);
	void DestroyFramebufferNoLock(vk::Framebuffer framebuffer);
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
	std::unique_ptr<FramebufferAllocator> _framebufferAllocator;
	std::unique_ptr<ShaderCompiler> _shaderCompiler;
	std::unique_ptr<ShaderManager> _shaderManager;
	std::unique_ptr<TransientAttachmentAllocator> _transientAttachmentAllocator;

	std::unique_ptr<BufferPool> _indexBlocks;
	std::vector<BufferBlock> _indexBlocksToCopy;
	std::unique_ptr<BufferPool> _uniformBlocks;
	std::vector<BufferBlock> _uniformBlocksToCopy;
	std::unique_ptr<BufferPool> _vertexBlocks;
	std::vector<BufferBlock> _vertexBlocksToCopy;

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

	VulkanObjectPool<BindlessDescriptorPool> _bindlessDescriptorPoolPool;
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
