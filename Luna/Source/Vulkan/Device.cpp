#include <Luna/Core/Threading.hpp>
#include <Luna/Utility/Hash.hpp>
#include <Luna/Vulkan/Buffer.hpp>
#include <Luna/Vulkan/CommandBuffer.hpp>
#include <Luna/Vulkan/CommandPool.hpp>
#include <Luna/Vulkan/Context.hpp>
#include <Luna/Vulkan/Device.hpp>
#include <Luna/Vulkan/Fence.hpp>
#include <Luna/Vulkan/Format.hpp>
#include <Luna/Vulkan/Image.hpp>
#include <Luna/Vulkan/RenderPass.hpp>
#include <Luna/Vulkan/Sampler.hpp>
#include <Luna/Vulkan/Semaphore.hpp>
#include <Luna/Vulkan/Shader.hpp>

namespace Luna {
namespace Vulkan {
#define DeviceLock()       std::lock_guard<std::mutex> _lockHolder(_lock.Lock)
#define DeviceMemoryLock() std::lock_guard<std::mutex> _lockHolderMemory(_lock.MemoryLock)
#define DeviceCacheLock()  RWSpinLockReadHolder _lockHolderCache(_lock.ReadOnlyCache)
#define DeviceFlush()                                   \
	std::unique_lock<std::mutex> _lockHolder(_lock.Lock); \
	_lock.Condition.wait(_lockHolder, [&]() { return _lock.Counter == 0; })

constexpr static const QueueType QueueFlushOrder[] = {QueueType::Transfer, QueueType::Graphics, QueueType::Compute};

static int64_t ConvertToSignedDelta(uint64_t startTicks, uint64_t endTicks, uint32_t validBits) {
	uint32_t shamt = 64 - validBits;
	startTicks <<= shamt;
	endTicks <<= shamt;
	auto ticksDelta = int64_t(endTicks - startTicks);
	ticksDelta >>= shamt;

	return ticksDelta;
}

Device::Device(Context& context)
		: _extensions(context._extensions),
			_instance(context._instance),
			_deviceInfo(context._deviceInfo),
			_queueInfo(context._queueInfo),
			_device(context._device) {
	_nextCookie.store(0, std::memory_order_relaxed);

	// Initialize Vulkan Memory Allocator
	{
#define VmaFn(name) .name = VULKAN_HPP_DEFAULT_DISPATCHER.name
		VmaVulkanFunctions vmaFunctions = {
			VmaFn(vkGetInstanceProcAddr),
			VmaFn(vkGetDeviceProcAddr),
			VmaFn(vkGetPhysicalDeviceProperties),
			VmaFn(vkGetPhysicalDeviceMemoryProperties),
			VmaFn(vkAllocateMemory),
			VmaFn(vkFreeMemory),
			VmaFn(vkMapMemory),
			VmaFn(vkUnmapMemory),
			VmaFn(vkFlushMappedMemoryRanges),
			VmaFn(vkInvalidateMappedMemoryRanges),
			VmaFn(vkBindBufferMemory),
			VmaFn(vkBindImageMemory),
			VmaFn(vkGetBufferMemoryRequirements),
			VmaFn(vkGetImageMemoryRequirements),
			VmaFn(vkCreateBuffer),
			VmaFn(vkDestroyBuffer),
			VmaFn(vkCreateImage),
			VmaFn(vkDestroyImage),
			VmaFn(vkCmdCopyBuffer),
			VmaFn(vkGetDeviceBufferMemoryRequirements),
			VmaFn(vkGetDeviceImageMemoryRequirements),
		};
#undef VmaFn
#define VmaFn(core) vmaFunctions.core##KHR = reinterpret_cast<PFN_##core##KHR>(VULKAN_HPP_DEFAULT_DISPATCHER.core)
		VmaFn(vkGetBufferMemoryRequirements2);
		VmaFn(vkGetImageMemoryRequirements2);
		VmaFn(vkBindBufferMemory2);
		VmaFn(vkBindImageMemory2);
		VmaFn(vkGetPhysicalDeviceMemoryProperties2);
#undef VmaFn

		VmaAllocatorCreateFlags allocatorFlags = VMA_ALLOCATOR_CREATE_EXTERNALLY_SYNCHRONIZED_BIT;
		if (_extensions.MemoryBudget) { allocatorFlags |= VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT; }
		if (_deviceInfo.EnabledFeatures.Vulkan12.bufferDeviceAddress) {
			allocatorFlags |= VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
		}
		const VmaAllocatorCreateInfo allocatorCI = {.flags                       = allocatorFlags,
		                                            .physicalDevice              = _deviceInfo.PhysicalDevice,
		                                            .device                      = _device,
		                                            .preferredLargeHeapBlockSize = 0,
		                                            .pAllocationCallbacks        = nullptr,
		                                            .pDeviceMemoryCallbacks      = nullptr,
		                                            .pHeapSizeLimit              = nullptr,
		                                            .pVulkanFunctions            = &vmaFunctions,
		                                            .instance                    = _instance,
		                                            .vulkanApiVersion            = VK_API_VERSION_1_3};
		const auto allocatorResult               = vmaCreateAllocator(&allocatorCI, &_allocator);
		if (allocatorResult != VK_SUCCESS) {
			Log::Error("Vulkan", "Failed to create memory allocator: {}", vk::to_string(vk::Result(allocatorResult)));
			throw std::runtime_error("Failed to create memory allocator");
		}
	}

	// Create Timeline Semaphores
	if (_deviceInfo.EnabledFeatures.Vulkan12.timelineSemaphore) {
		const vk::SemaphoreCreateInfo semaphoreCI;
		const vk::SemaphoreTypeCreateInfo semaphoreType(vk::SemaphoreType::eTimeline, 0);
		const vk::StructureChain chain(semaphoreCI, semaphoreType);
		for (auto& queue : _queueData) {
			queue.TimelineSemaphore = _device.createSemaphore(chain.get());
			Log::Trace("Vulkan", "Timeline Semaphore created.");
		}
	}

	for (int i = 0; i < StockSamplerCount; ++i) {
		const auto type = static_cast<StockSampler>(i);
		SamplerCreateInfo info{};
		info.MinLod        = 0.0f;
		info.MaxLod        = VK_LOD_CLAMP_NONE;
		info.MaxAnisotropy = 1.0f;

		switch (type) {
			case StockSampler::LinearShadow:
			case StockSampler::NearestShadow:
				info.CompareEnable = VK_TRUE;
				info.CompareOp     = vk::CompareOp::eLessOrEqual;
				break;

			default:
				info.CompareEnable = VK_FALSE;
				break;
		}

		switch (type) {
			case StockSampler::DefaultGeometryFilterClamp:
			case StockSampler::DefaultGeometryFilterWrap:
			case StockSampler::TrilinearClamp:
			case StockSampler::TrilinearWrap:
				info.MipmapMode = vk::SamplerMipmapMode::eLinear;
				break;

			default:
				info.MipmapMode = vk::SamplerMipmapMode::eNearest;
				break;
		}

		switch (type) {
			case StockSampler::DefaultGeometryFilterClamp:
			case StockSampler::DefaultGeometryFilterWrap:
			case StockSampler::LinearClamp:
			case StockSampler::LinearShadow:
			case StockSampler::LinearWrap:
			case StockSampler::TrilinearClamp:
			case StockSampler::TrilinearWrap:
				info.MagFilter = vk::Filter::eLinear;
				info.MinFilter = vk::Filter::eLinear;
				break;

			default:
				info.MagFilter = vk::Filter::eNearest;
				info.MinFilter = vk::Filter::eNearest;
				break;
		}

		switch (type) {
			case StockSampler::DefaultGeometryFilterClamp:
			case StockSampler::LinearClamp:
			case StockSampler::LinearShadow:
			case StockSampler::NearestClamp:
			case StockSampler::NearestShadow:
			case StockSampler::TrilinearClamp:
				info.AddressModeU = vk::SamplerAddressMode::eClampToEdge;
				info.AddressModeV = vk::SamplerAddressMode::eClampToEdge;
				info.AddressModeW = vk::SamplerAddressMode::eClampToEdge;
				break;

			default:
				info.AddressModeU = vk::SamplerAddressMode::eRepeat;
				info.AddressModeV = vk::SamplerAddressMode::eRepeat;
				info.AddressModeW = vk::SamplerAddressMode::eRepeat;
		}

		switch (type) {
			case StockSampler::DefaultGeometryFilterClamp:
			case StockSampler::DefaultGeometryFilterWrap:
				if (_deviceInfo.EnabledFeatures.Core.samplerAnisotropy) {
					info.AnisotropyEnable = VK_TRUE;
					info.MaxAnisotropy    = std::min(16.0f, _deviceInfo.Properties.Core.limits.maxSamplerAnisotropy);
				}
				info.MipLodBias = 0.0f;
				break;

			default:
				break;
		}

		_stockSamplers[i] = RequestImmutableSampler(info);
	}

	// Create Frame Contexts
	CreateFrameContexts(2);
}

Device::~Device() noexcept {
	_swapchainAcquire.Reset();
	_swapchainRelease.Reset();
	_swapchainImages.clear();

	WaitIdle();

	if (_deviceInfo.EnabledFeatures.Vulkan12.timelineSemaphore) {
		for (auto& queue : _queueData) {
			if (queue.TimelineSemaphore) {
				_device.destroySemaphore(queue.TimelineSemaphore);
				queue.TimelineSemaphore = nullptr;
			}
		}
	}

	_immutableSamplers.Clear();

	_frameContexts.clear();
	for (auto& semaphore : _availableSemaphores) { _device.destroySemaphore(semaphore); }
	for (auto& fence : _availableFences) { _device.destroyFence(fence); }

	vmaDestroyAllocator(_allocator);
}

/* ==============================================
** ===== Public Object Management Functions =====
*  ============================================== */
BufferHandle Device::CreateBuffer(const BufferCreateInfo& createInfo,
                                  const void* initialData,
                                  const std::string& debugName) {
	const bool zeroInitialize = createInfo.Flags & BufferCreateFlagBits::ZeroInitialize;
	if (createInfo.Size == 0) {
		Log::Error("Vulkan", "Cannot create a buffer with a size of 0");

		return {};
	}

	if (initialData && zeroInitialize) {
		Log::Error("Vulkan", "Cannot create a buffer with initial data and zero-initialize flags at the same time");
	}

	BufferHandle handle;
	try {
		handle = BufferHandle(_bufferPool.Allocate(*this, createInfo, initialData, debugName));
	} catch (const std::exception& e) {
		Log::Error("Vulkan", "Failed to create buffer: {}", e.what());

		return {};
	}

	// Zero-initialize or copy initial data for our buffer
	if (initialData) {
		handle->WriteData(initialData, createInfo.Size, 0);
	} else if (zeroInitialize) {
		handle->FillData(0, createInfo.Size, 0);
	}

	return handle;
}

ImageHandle Device::CreateImage(const ImageCreateInfo& createInfo,
                                const ImageInitialData* initialData,
                                const std::string& debugName) {
	ImageHandle handle;
	try {
		handle = ImageHandle(_imagePool.Allocate(*this, createInfo, initialData, debugName));
	} catch (const std::exception& e) {
		Log::Error("Vulkan", "Failed to create image: {}", e.what());

		return {};
	}

	return handle;
}

ImageViewHandle Device::CreateImageView(const ImageViewCreateInfo& createInfo, const std::string& debugName) {
	ImageViewHandle handle;
	try {
		handle = ImageViewHandle(_imageViewPool.Allocate(*this, createInfo));
	} catch (const std::exception& e) {
		Log::Error("Vulkan", "Failed to create image view: {}", e.what());

		return {};
	}

	return handle;
}

SamplerHandle Device::CreateSampler(const SamplerCreateInfo& samplerCI, const std::string& debugName) {
	try {
		return SamplerHandle(_samplerPool.Allocate(*this, samplerCI, false));
	} catch (const std::exception& e) {
		Log::Error("Vulkan", "Failed to create Sampler: {}", e.what());

		return {};
	}
}

const Sampler& Device::GetStockSampler(StockSampler type) const {
	return _stockSamplers[int(type)]->GetSampler();
}

RenderPassInfo Device::GetSwapchainRenderPass(SwapchainRenderPassType type) {
	RenderPassInfo info       = {};
	info.ColorAttachmentCount = 1;
	info.ColorAttachments[0]  = &GetSwapchainView();
	info.ClearColors[0]       = vk::ClearColorValue(0.0f, 0.0f, 0.0f, 1.0f);
	info.ClearDepthStencil    = vk::ClearDepthStencilValue(1.0f, 0);
	info.ClearAttachmentMask  = 1 << 0;
	info.StoreAttachmentMask  = 1 << 0;

	const auto& imageCI = info.ColorAttachments[0]->GetImage().GetCreateInfo();
	const vk::Extent2D extent(imageCI.Width, imageCI.Height);

	if (type == SwapchainRenderPassType::Depth) {
		info.Flags |= RenderPassFlagBits::ClearDepthStencil;
		auto depth                  = RequestTransientAttachment(extent, GetDefaultDepthFormat());
		info.DepthStencilAttachment = &depth->GetView();
	} else if (type == SwapchainRenderPassType::DepthStencil) {
		info.Flags |= RenderPassFlagBits::ClearDepthStencil;
		auto depthStencil           = RequestTransientAttachment(extent, GetDefaultDepthStencilFormat());
		info.DepthStencilAttachment = &depthStencil->GetView();
	}

	return info;
}

ImageView& Device::GetSwapchainView() {
	return GetSwapchainView(_swapchainIndex);
}

const ImageView& Device::GetSwapchainView() const {
	return GetSwapchainView(_swapchainIndex);
}

ImageView& Device::GetSwapchainView(uint32_t index) {
	return _swapchainImages[_swapchainIndex]->GetView();
}

const ImageView& Device::GetSwapchainView(uint32_t index) const {
	return _swapchainImages[_swapchainIndex]->GetView();
}

TimestampReport Device::GetTimestampReport(const std::string& name) {
	Hasher h(name);
	const auto hash = h.Get();

	const auto* ts = _timestamps.Find(hash);
	if (ts) {
		return TimestampReport{.TimePerAccumulation          = ts->GetTimePerAccumulation(),
		                       .TimePerFrameContext          = ts->GetTotalTime(),
		                       .AccumulationsPerFrameContext = double(ts->GetTotalAccumulations())};
	}

	return {};
}

void Device::RegisterTimeInterval(QueryResultHandle start, QueryResultHandle end, const std::string& name) {
	DeviceLock();
	RegisterTimeIntervalNoLock(std::move(start), std::move(end), name);
}

DescriptorSetAllocator* Device::RequestDescriptorSetAllocator(const DescriptorSetLayout& layout,
                                                              const vk::ShaderStageFlags* stagesForBindings) {
	Hasher h;
	h.Data(sizeof(DescriptorSetLayout), &layout);
	h.Data(sizeof(vk::ShaderStageFlags) * MaxDescriptorBindings, stagesForBindings);
	const auto hash = h.Get();

	DeviceCacheLock();

	auto* ret = _descriptorSetAllocators.Find(hash);
	if (!ret) { ret = _descriptorSetAllocators.EmplaceYield(hash, hash, *this, layout, stagesForBindings); }

	return ret;
}

const ImmutableSampler* Device::RequestImmutableSampler(const SamplerCreateInfo& samplerCI) {
	const auto hash = Hasher(samplerCI).Get();

	DeviceCacheLock();
	auto* sampler = _immutableSamplers.Find(hash);
	if (!sampler) { sampler = _immutableSamplers.EmplaceYield(hash, hash, *this, samplerCI); }

	return sampler;
}

Program* Device::RequestProgram(Shader* compute) {
	return ProgramBuilder{*this}.Compute(compute).Build();
}

Program* Device::RequestProgram(const std::array<Shader*, ShaderStageCount>& shaders) {
	Hasher h;
	for (const auto shader : shaders) { h(shader ? shader->GetHash() : Hash(0)); }
	const auto hash = h.Get();

	bool anyShaders = false;
	for (const auto shader : shaders) {
		if (shader != nullptr) { anyShaders = true; }
	}
	if (!anyShaders) { return nullptr; }

	DeviceCacheLock();

	Program* ret = _programs.Find(hash);
	if (!ret) {
		try {
			ret = _programs.EmplaceYield(hash, hash, *this, shaders);
		} catch (const std::exception& e) {
			Log::Error("Vulkan", "Failed to create program: {}", e.what());

			return nullptr;
		}
	}

	return ret;
}

Program* Device::RequestProgram(const std::vector<uint32_t>& compCode) {
	return RequestProgram(RequestShader(compCode));
}

Program* Device::RequestProgram(size_t compCodeSize, const void* compCode) {
	return RequestProgram(RequestShader(compCodeSize, compCode));
}

Program* Device::RequestProgram(Shader* vertex, Shader* fragment) {
	return ProgramBuilder{*this}.Vertex(vertex).Fragment(fragment).Build();
}

Program* Device::RequestProgram(const std::vector<uint32_t>& vertexCode, const std::vector<uint32_t>& fragmentCode) {
	return RequestProgram(RequestShader(vertexCode), RequestShader(fragmentCode));
}

Program* Device::RequestProgram(size_t vertexCodeSize,
                                const void* vertexCode,
                                size_t fragmentCodeSize,
                                const void* fragmentCode) {
	return RequestProgram(RequestShader(vertexCodeSize, vertexCode), RequestShader(fragmentCodeSize, fragmentCode));
}

SemaphoreHandle Device::RequestProxySemaphore() {
	return SemaphoreHandle(_semaphorePool.Allocate(*this));
}

SemaphoreHandle Device::RequestSemaphore(const std::string& debugName) {
	DeviceLock();
	auto semaphore = AllocateSemaphore(debugName);

	return SemaphoreHandle(_semaphorePool.Allocate(*this, semaphore, false, true, debugName));
}

Shader* Device::RequestShader(Hash hash) {
	DeviceCacheLock();

	return _shaders.Find(hash);
}

Shader* Device::RequestShader(const std::vector<uint32_t>& code) {
	return RequestShader(size_t(code.size()) * sizeof(uint32_t), code.data());
}

Shader* Device::RequestShader(size_t codeSize, const void* code) {
	Hasher h;
	h(codeSize);
	h.Data(codeSize, code);
	const auto hash = h.Get();

	DeviceCacheLock();

	auto* ret = _shaders.Find(hash);
	if (!ret) { ret = _shaders.EmplaceYield(hash, hash, *this, codeSize, code); }

	return ret;
}

ImageHandle Device::RequestTransientAttachment(const vk::Extent2D& extent,
                                               vk::Format format,
                                               uint32_t index,
                                               vk::SampleCountFlagBits samples,
                                               uint32_t arrayLayers) {
	Hasher h;
	h(extent.width);
	h(extent.height);
	h(format);
	h(index);
	h(samples);
	h(arrayLayers);
	const auto hash = h.Get();

	std::lock_guard<std::mutex> lock(_lock.TransientAttachmentLock);
	auto* node = _transientAttachments.Request(hash);
	if (node) { return node->Image; }

	const auto imageCI =
		ImageCreateInfo::TransientRenderTarget(format, extent).SetSamples(samples).SetArrayLayers(arrayLayers);
	node = _transientAttachments.Emplace(hash, CreateImage(imageCI));
	node->Image->SetInternalSync();
	node->Image->GetView().SetInternalSync();

	return node->Image;
}

QueryResultHandle Device::WriteTimestamp(vk::CommandBuffer cmd, vk::PipelineStageFlags2 stages) {
	DeviceLock();
	return WriteTimestampNoLock(cmd, stages);
}

void Device::SetObjectName(vk::ObjectType type, uint64_t handle, const std::string& name) {
	if (!_extensions.DebugUtils) { return; }

	const vk::DebugUtilsObjectNameInfoEXT nameInfo(type, handle, name.c_str());
	_device.setDebugUtilsObjectNameEXT(nameInfo);
}

/* ============================================
** ===== Public Synchronization Functions =====
*  ============================================ */
void Device::AddWaitSemaphore(CommandBufferType cmdType,
                              SemaphoreHandle semaphore,
                              vk::PipelineStageFlags2 stages,
                              bool flush) {
	DeviceLock();
	AddWaitSemaphoreNoLock(GetQueueType(cmdType), std::move(semaphore), stages, flush);
}

SemaphoreHandle Device::ConsumeReleaseSemaphore() noexcept {
	return std::move(_swapchainRelease);
}

void Device::EndFrame() {
	DeviceFlush();
	EndFrameNoLock();
}

void Device::FlushFrame() {
	DeviceLock();
	FlushFrameNoLock();
}

void Device::NextFrame() {
	DeviceFlush();

	EndFrameNoLock();

	_framebuffers.BeginFrame();
	_transientAttachments.BeginFrame();

	_currentFrameContext = (_currentFrameContext + 1) % _frameContexts.size();
	vmaSetCurrentFrameIndex(_allocator, _currentFrameContext);
	Frame().Begin();
}

CommandBufferHandle Device::RequestCommandBuffer(CommandBufferType type, const std::string& debugName) {
	return RequestCommandBufferForThread(Threading::GetThreadID(), type, debugName);
}

CommandBufferHandle Device::RequestCommandBufferForThread(uint32_t threadIndex,
                                                          CommandBufferType type,
                                                          const std::string& debugName) {
	DeviceLock();

	return RequestCommandBufferNoLock(threadIndex, type, debugName);
}

void Device::SetAcquireSemaphore(uint32_t imageIndex, SemaphoreHandle semaphore) {
	_swapchainAcquire         = std::move(semaphore);
	_swapchainAcquireConsumed = false;
	_swapchainIndex           = imageIndex;

	if (_swapchainAcquire) { _swapchainAcquire->SetInternalSync(); }
}

void Device::SetupSwapchain(const vk::Extent2D& extent,
                            const vk::SurfaceFormatKHR& format,
                            const std::vector<vk::Image>& images) {
	DeviceFlush();
	WaitIdleNoLock();

	const auto imageCI = ImageCreateInfo::RenderTarget(format.format, extent.width, extent.height);

	_swapchainAcquireConsumed = false;
	_swapchainImages.clear();
	_swapchainImages.reserve(images.size());
	_swapchainIndex = std::numeric_limits<uint32_t>::max();

	for (size_t i = 0; i < images.size(); ++i) {
		const auto& image = images[i];

		const vk::ImageViewCreateInfo viewCI({},
		                                     image,
		                                     vk::ImageViewType::e2D,
		                                     format.format,
		                                     vk::ComponentMapping(),
		                                     vk::ImageSubresourceRange(FormatAspectFlags(format.format), 0, 1, 0, 1));
		auto imageView = _device.createImageView(viewCI);
		Log::Trace("Vulkan", "Image View created.");

		Image* img = _imagePool.Allocate(*this, imageCI, image, VmaAllocation{}, imageView);
		ImageHandle handle(img);
		handle->DisownImage();
		handle->DisownMemory();
		handle->SetInternalSync();
		handle->GetView().SetInternalSync();
		handle->SetSwapchainLayout(vk::ImageLayout::ePresentSrcKHR);

		_swapchainImages.push_back(handle);
	}
}

bool Device::SwapchainAcquired() const {
	return _swapchainAcquireConsumed;
}

void Device::Submit(CommandBufferHandle& commandBuffer, FenceHandle* fence, std::vector<SemaphoreHandle>* semaphores) {
	DeviceLock();
	SubmitNoLock(std::move(commandBuffer), fence, semaphores);
}

void Device::WaitIdle() {
	DeviceFlush();
	WaitIdleNoLock();
}

/* ===============================================
** ===== Private Object Management Functions =====
*  =============================================== */
vk::Fence Device::AllocateFence() {
	if (_availableFences.empty()) {
		const vk::FenceCreateInfo fenceCI;

		return _device.createFence(fenceCI);
	} else {
		auto fence = _availableFences.back();
		_availableFences.pop_back();

		return fence;
	}
}

vk::Semaphore Device::AllocateSemaphore(const std::string& debugName) {
	vk::Semaphore semaphore;

	if (_availableSemaphores.empty()) {
		const vk::SemaphoreCreateInfo semaphoreCI;
		semaphore = _device.createSemaphore(semaphoreCI);
		Log::Trace("Vulkan", "Semaphore created.");
	} else {
		semaphore = _availableSemaphores.back();
		_availableSemaphores.pop_back();
	}

	if (!debugName.empty()) { SetObjectName(semaphore, debugName); }

	return semaphore;
}

void Device::ConsumeSemaphore(vk::Semaphore semaphore) {
	DeviceLock();
	ConsumeSemaphoreNoLock(semaphore);
}

void Device::ConsumeSemaphoreNoLock(vk::Semaphore semaphore) {
	Frame().SemaphoresToConsume.push_back(semaphore);
}

void Device::DestroyBuffer(vk::Buffer buffer) {
	DeviceLock();
	DestroyBufferNoLock(buffer);
}

void Device::DestroyBufferNoLock(vk::Buffer buffer) {
	Frame().BuffersToDestroy.push_back(buffer);
}

void Device::DestroyFramebuffer(vk::Framebuffer framebuffer) {
	DeviceLock();
	DestroyFramebufferNoLock(framebuffer);
}

void Device::DestroyFramebufferNoLock(vk::Framebuffer framebuffer) {
	Frame().FramebuffersToDestroy.push_back(framebuffer);
}

void Device::DestroyImage(vk::Image image) {
	DeviceLock();
	DestroyImageNoLock(image);
}

void Device::DestroyImageNoLock(vk::Image image) {
	Frame().ImagesToDestroy.push_back(image);
}

void Device::DestroyImageView(vk::ImageView imageView) {
	DeviceLock();
	DestroyImageViewNoLock(imageView);
}

void Device::DestroyImageViewNoLock(vk::ImageView imageView) {
	Frame().ImageViewsToDestroy.push_back(imageView);
}

void Device::DestroySampler(vk::Sampler sampler) {
	DeviceLock();
	DestroySamplerNoLock(sampler);
}

void Device::DestroySamplerNoLock(vk::Sampler sampler) {
	Frame().SamplersToDestroy.push_back(sampler);
}

void Device::DestroySemaphore(vk::Semaphore semaphore) {
	DeviceLock();
	DestroySemaphoreNoLock(semaphore);
}

void Device::DestroySemaphoreNoLock(vk::Semaphore semaphore) {
	Frame().SemaphoresToDestroy.push_back(semaphore);
}

void Device::FreeAllocation(VmaAllocation allocation, bool mapped) {
	DeviceLock();
	FreeAllocationNoLock(allocation, mapped);
}

void Device::FreeAllocationNoLock(VmaAllocation allocation, bool mapped) {
	Frame().AllocationsToFree.push_back(allocation);
	if (mapped) { Frame().AllocationsToUnmap.push_back(allocation); }
}

void Device::FreeFence(vk::Fence fence) {
	_availableFences.push_back(fence);
}

void Device::FreeSemaphore(vk::Semaphore semaphore) {
	_availableSemaphores.push_back(semaphore);
}

TimestampInterval* Device::GetTimestampTag(const std::string& name) {
	Hasher h(name);
	const auto hash = h.Get();

	return _timestamps.EmplaceYield(hash, name);
}

void Device::RegisterTimeIntervalNoLock(QueryResultHandle start, QueryResultHandle end, const std::string& name) {
	if (start && end) {
		auto* timestampTag = GetTimestampTag(name);
		Frame().TimestampIntervals.emplace_back(start, end, timestampTag);
	}
}

void Device::RecycleSemaphore(vk::Semaphore semaphore) {
	DeviceLock();
	RecycleSemaphoreNoLock(semaphore);
}

void Device::RecycleSemaphoreNoLock(vk::Semaphore semaphore) {
	Frame().SemaphoresToRecycle.push_back(semaphore);
}

const Framebuffer& Device::RequestFramebuffer(const RenderPassInfo& rpInfo) {
	Hasher h;

	auto& rp = RequestRenderPass(rpInfo, true);
	h(rp.GetHash());

	for (uint32_t i = 0; i < rpInfo.ColorAttachmentCount; ++i) { h(rpInfo.ColorAttachments[i]->GetCookie()); }
	if (rpInfo.DepthStencilAttachment) { h(rpInfo.DepthStencilAttachment->GetCookie()); }
	h(rpInfo.ArrayLayers > 1 ? 0u : rpInfo.BaseLayer);

	const auto hash = h.Get();

	std::lock_guard<std::mutex> lock(_lock.FramebufferLock);
	auto* node = _framebuffers.Request(hash);
	if (node) { return *node; }

	return *_framebuffers.Emplace(hash, *this, rp, rpInfo);
}

PipelineLayout* Device::RequestPipelineLayout(const ProgramResourceLayout& resourceLayout) {
	const auto hash = Hasher(resourceLayout).Get();

	auto* ret = _pipelineLayouts.Find(hash);
	if (!ret) { ret = _pipelineLayouts.EmplaceYield(hash, hash, *this, resourceLayout); }

	return ret;
}

const RenderPass& Device::RequestRenderPass(const RenderPassInfo& rpInfo, bool compatible) {
	Hasher h;

	std::array<vk::Format, MaxColorAttachments> colorFormats;
	colorFormats.fill(vk::Format::eUndefined);
	vk::Format depthFormat = vk::Format::eUndefined;
	uint32_t lazy          = 0;

	for (uint32_t i = 0; i < rpInfo.ColorAttachmentCount; ++i) {
		colorFormats[i] = rpInfo.ColorAttachments[i]->GetFormat();
		if (rpInfo.ColorAttachments[i]->GetImage().GetCreateInfo().Domain == ImageDomain::Transient) { lazy |= 1u << i; }
		h(rpInfo.ColorAttachments[i]->GetImage().GetSwapchainLayout());
	}

	if (rpInfo.DepthStencilAttachment) {
		if (rpInfo.DepthStencilAttachment->GetImage().GetCreateInfo().Domain == ImageDomain::Transient) {
			lazy |= 1u << rpInfo.ColorAttachmentCount;
		}
	}

	if (rpInfo.ArrayLayers > 1) {
		h(rpInfo.BaseLayer);
		h(rpInfo.ArrayLayers);
	} else {
		h(uint32_t(0));
		h(rpInfo.ArrayLayers);
	}

	h(rpInfo.Subpasses.size());
	for (const auto& subpass : rpInfo.Subpasses) {
		h(subpass.ColorAttachmentCount);
		h(subpass.InputAttachmentCount);
		h(subpass.ResolveAttachmentCount);
		h(subpass.DepthStencil);
		for (uint32_t i = 0; i < subpass.ColorAttachmentCount; ++i) { h(subpass.ColorAttachments[i]); }
		for (uint32_t i = 0; i < subpass.InputAttachmentCount; ++i) { h(subpass.InputAttachments[i]); }
		for (uint32_t i = 0; i < subpass.ResolveAttachmentCount; ++i) { h(subpass.ResolveAttachments[i]); }
	}

	h(rpInfo.ColorAttachmentCount);
	for (const auto format : colorFormats) { h(format); }
	h(rpInfo.DepthStencilAttachment ? rpInfo.DepthStencilAttachment->GetFormat() : vk::Format::eUndefined);

	if (!compatible) {
		h(rpInfo.Flags);
		h(rpInfo.ClearAttachmentMask);
		h(rpInfo.LoadAttachmentMask);
		h(rpInfo.StoreAttachmentMask);
	}
	h(lazy);

	const auto hash = h.Get();
	auto* ret       = _renderPasses.Find(hash);
	if (!ret) { ret = _renderPasses.EmplaceYield(hash, hash, *this, rpInfo); }

	return *ret;
}

void Device::ResetFence(vk::Fence fence, bool observedWait) {
	DeviceLock();
	ResetFenceNoLock(fence, observedWait);
}

void Device::ResetFenceNoLock(vk::Fence fence, bool observedWait) {
	if (observedWait) {
		_device.resetFences(fence);
		FreeFence(fence);
	} else {
		Frame().FencesToRecycle.push_back(fence);
	}
}

QueryResultHandle Device::WriteTimestampNoLock(vk::CommandBuffer cmd, vk::PipelineStageFlags2 stages) {
	return Frame().QueryPool.WriteTimestamp(cmd, stages);
}

/* =============================================
** ===== Private Synchronization Functions =====
*  ============================================= */
void Device::AddWaitSemaphoreNoLock(QueueType queueType,
                                    SemaphoreHandle semaphore,
                                    vk::PipelineStageFlags2 stages,
                                    bool flush) {
	if (flush) { FlushQueue(queueType); }

	auto& queueData = _queueData[int(queueType)];
	semaphore->SetPendingWait();
	queueData.WaitSemaphores.push_back(semaphore);
	queueData.WaitStages.push_back(stages);
	queueData.NeedsFence = true;
}

void Device::EndFrameNoLock() {
	for (const auto type : QueueFlushOrder) {
		if (_queueData[int(type)].NeedsFence || !Frame().Submissions[int(type)].empty() ||
		    !Frame().SemaphoresToConsume.empty()) {
			InternalFence fence = {};
			SubmitQueue(type, &fence, nullptr);
			if (fence.Fence) {
				Frame().FencesToAwait.push_back(fence.Fence);
				Frame().FencesToRecycle.push_back(fence.Fence);
			}
			_queueData[int(type)].NeedsFence = false;
		}
	}
}

void Device::FlushFrameNoLock() {
	for (const auto type : QueueFlushOrder) { FlushQueue(type); }
}

void Device::FlushQueue(QueueType queueType) {
	if (!_queueInfo.Queue(queueType)) { return; }

	SubmitQueue(queueType, nullptr, nullptr);
}

CommandBufferHandle Device::RequestCommandBufferNoLock(uint32_t threadIndex,
                                                       CommandBufferType type,
                                                       const std::string& debugName) {
	const auto queueType = GetQueueType(type);
	auto& commandPool    = Frame().CommandPools[int(queueType)][threadIndex];
	auto commandBuffer   = commandPool.RequestCommandBuffer();
	++_lock.Counter;

	CommandBufferHandle handle(_commandBufferPool.Allocate(*this, type, commandBuffer, threadIndex, debugName));
	handle->Begin();

	return handle;
}

void Device::SubmitNoLock(CommandBufferHandle commandBuffer,
                          FenceHandle* fence,
                          std::vector<SemaphoreHandle>* semaphores) {
	const auto commandBufferType = commandBuffer->GetCommandBufferType();
	const auto queueType         = GetQueueType(commandBufferType);
	const bool hasSemaphores     = semaphores && semaphores->size() > 0;
	auto& submissions            = Frame().Submissions[int(queueType)];

	commandBuffer->End();
	submissions.push_back(std::move(commandBuffer));

	InternalFence signalFence;
	if (fence || hasSemaphores) { SubmitQueue(queueType, fence ? &signalFence : nullptr, semaphores); }

	if (fence) {
		if (signalFence.TimelineValue) {
			*fence = FenceHandle(_fencePool.Allocate(*this, signalFence.TimelineSemaphore, signalFence.TimelineValue));
		} else {
			*fence = FenceHandle(_fencePool.Allocate(*this, signalFence.Fence));
		}
	}

	--_lock.Counter;
	_lock.Condition.notify_all();
}

void Device::SubmitQueue(QueueType queueType, InternalFence* signalFence, std::vector<SemaphoreHandle>* semaphores) {
	// Ensure all pending operations on the transfer queue are submitted first.
	if (queueType != QueueType::Transfer) { FlushQueue(QueueType::Transfer); }

	// Gather a few useful locals.
	const bool hasSemaphores = semaphores && semaphores->size() > 0;
	auto& queueData          = _queueData[int(queueType)];
	auto& submissions        = Frame().Submissions[int(queueType)];
	vk::Queue queue          = _queueInfo.Queue(queueType);

	// Increment our timeline value for this queue.
	vk::Semaphore timelineSemaphore        = queueData.TimelineSemaphore;
	uint64_t timelineValue                 = ++queueData.TimelineValue;
	Frame().TimelineValues[int(queueType)] = queueData.TimelineValue;

	// Here, we batch all of the pending command buffers into as few submissions as possible. We should only need to make
	// a new batch if a signal semaphore is given.
	constexpr static const int MaxSubmissions = 8;
	struct SubmitBatch {
		std::vector<vk::CommandBufferSubmitInfo> CommandBuffers;
		std::vector<vk::SemaphoreSubmitInfo> SignalSemaphores;
		std::vector<vk::SemaphoreSubmitInfo> WaitSemaphores;
	};
	uint32_t currentBatch = 0;
	std::array<SubmitBatch, MaxSubmissions> batches;

	// Define a few helper functions to aid us in dealing with batches.
	const auto Batch     = [&]() -> SubmitBatch& { return batches[currentBatch]; };
	const auto NextBatch = [&]() -> void {
		if (Batch().WaitSemaphores.empty() && Batch().CommandBuffers.empty() && Batch().SignalSemaphores.empty()) {
			return;
		}

		++currentBatch;

		if (currentBatch >= MaxSubmissions) { throw std::runtime_error("Too many submission batches!"); }
	};
	const auto AddWaitSemaphore = [&](vk::Semaphore semaphore, uint64_t value, vk::PipelineStageFlags2 stages) -> void {
		if (!Batch().CommandBuffers.empty() || !Batch().SignalSemaphores.empty()) { NextBatch(); }

		Batch().WaitSemaphores.emplace_back(semaphore, value, stages);
	};
	const auto AddCommandBuffer = [&](vk::CommandBuffer commandBuffer) -> void {
		if (!Batch().SignalSemaphores.empty()) { NextBatch(); }

		Batch().CommandBuffers.emplace_back(commandBuffer);
	};
	const auto AddSignalSemaphore = [&](vk::Semaphore semaphore, uint64_t value, vk::PipelineStageFlags2 stages) -> void {
		Batch().SignalSemaphores.emplace_back(semaphore, value, stages);
	};

	// Gather all of the semaphores we need to wait on before beginning this submission.
	for (size_t i = 0; i < queueData.WaitSemaphores.size(); ++i) {
		auto& semaphoreHandle    = queueData.WaitSemaphores[i];
		const auto waitStages    = queueData.WaitStages[i];
		const auto timelineValue = semaphoreHandle->GetTimelineValue();
		const auto semaphore     = semaphoreHandle->Consume();

		Batch().WaitSemaphores.emplace_back(semaphore, timelineValue, waitStages);
		if (timelineValue == 0) { RecycleSemaphoreNoLock(semaphore); }
	}
	queueData.WaitSemaphores.clear();
	queueData.WaitStages.clear();

	// Add all of our command buffers.
	for (auto& commandBuffer : submissions) {
		const auto swapchainStages = commandBuffer->GetSwapchainStages();
		if (swapchainStages && !_swapchainAcquireConsumed) {
			if (_swapchainAcquire && _swapchainAcquire->GetSemaphore()) {
				const auto value = _swapchainAcquire->GetTimelineValue();
				AddWaitSemaphore(_swapchainAcquire->GetSemaphore(), value, swapchainStages);
				if (!value) { Frame().SemaphoresToRecycle.push_back(_swapchainAcquire->GetSemaphore()); }

				_swapchainAcquire->Consume();
				_swapchainAcquireConsumed = true;
				_swapchainAcquire.Reset();
			}

			AddCommandBuffer(commandBuffer->GetCommandBuffer());

			vk::Semaphore release = AllocateSemaphore();
			_swapchainRelease     = SemaphoreHandle(_semaphorePool.Allocate(*this, release, true, true));
			_swapchainRelease->SetInternalSync();
			AddSignalSemaphore(release, 0, {});
		} else {
			AddCommandBuffer(commandBuffer->GetCommandBuffer());
		}
	}
	submissions.clear();

	// Add all of our consumed semaphores to the last batch, so the wait happens as late as possible.
	for (auto semaphore : Frame().SemaphoresToConsume) {
		AddWaitSemaphore(semaphore, 0, vk::PipelineStageFlagBits2::eNone);
		Frame().SemaphoresToRecycle.push_back(semaphore);
	}
	Frame().SemaphoresToConsume.clear();

	// Add our outgoing signal fences/semaphores, if applicable.
	if (_deviceInfo.EnabledFeatures.Vulkan12.timelineSemaphore) {
		AddSignalSemaphore(timelineSemaphore, timelineValue, vk::PipelineStageFlagBits2::eAllCommands);

		if (signalFence) {
			signalFence->Fence             = nullptr;
			signalFence->TimelineSemaphore = timelineSemaphore;
			signalFence->TimelineValue     = timelineValue;
		}

		if (hasSemaphores) {
			for (size_t i = 0; i < semaphores->size(); ++i) {
				(*semaphores)[i] = SemaphoreHandle(_semaphorePool.Allocate(*this, timelineSemaphore, timelineValue, false));
				(*semaphores)[i]->SignalExternal();
			}
		}
	} else {
		if (signalFence) {
			signalFence->Fence             = AllocateFence();
			signalFence->TimelineSemaphore = nullptr;
			signalFence->TimelineValue     = 0;
		}

		if (hasSemaphores) {
			for (size_t i = 0; i < semaphores->size(); ++i) {
				auto semaphore = AllocateSemaphore();
				AddSignalSemaphore(semaphore, 0, vk::PipelineStageFlagBits2::eAllCommands);
				(*semaphores)[i] = SemaphoreHandle(_semaphorePool.Allocate(*this, semaphore, true, true));
			}
		}
	}

	// Now we have everything together, we can build the list of submission structures.
	std::array<vk::SubmitInfo2, MaxSubmissions> submits;
	for (int i = 0; i < MaxSubmissions; ++i) {
		submits[i] = vk::SubmitInfo2({}, batches[i].WaitSemaphores, batches[i].CommandBuffers, batches[i].SignalSemaphores);
	}

	// Remove any empty submissions.
	uint32_t submitCount = 0;
	for (size_t i = 0; i < MaxSubmissions; ++i) {
		if (submits[i].waitSemaphoreInfoCount || submits[i].commandBufferInfoCount || submits[i].signalSemaphoreInfoCount) {
			if (i != submitCount) { submits[submitCount] = submits[i]; }
			++submitCount;
		}
	}

	// Finally, perform the actual queue submissions.
	const auto submitResult = queue.submit2(submitCount, submits.data(), signalFence ? signalFence->Fence : nullptr);
	if (submitResult != vk::Result::eSuccess) {
		Log::Error("Vulkan", "Failed to submit command buffers: {}", vk::to_string(submitResult));
	}

	if (!_deviceInfo.EnabledFeatures.Vulkan12.timelineSemaphore) { queueData.NeedsFence = true; }
}

void Device::SubmitStaging(CommandBufferHandle commandBuffer, vk::PipelineStageFlags2 stages, bool flush) {
	DeviceLock();
	SubmitStagingNoLock(commandBuffer, stages, flush);
}

void Device::SubmitStagingNoLock(CommandBufferHandle commandBuffer, vk::PipelineStageFlags2 stages, bool flush) {
	// We perform all staging transfers/fills on the transfer queue, if available.
	// Because this can be a separate queue, the only way to synchronize it with other queues is via semaphores.

	// Submit our staging work, signalling each of the semaphores given.
	std::vector<SemaphoreHandle> semaphores(2);
	SubmitNoLock(commandBuffer, nullptr, &semaphores);

	// Set each semaphore as internally synchronized.
	for (auto& semaphore : semaphores) { semaphore->SetInternalSync(); }

	AddWaitSemaphoreNoLock(QueueType::Graphics, semaphores[0], vk::PipelineStageFlagBits2::eAllGraphics, true);
	AddWaitSemaphoreNoLock(QueueType::Compute, semaphores[1], vk::PipelineStageFlagBits2::eComputeShader, true);
}

/** Flushes all pending work and waits for the Vulkan device to be idle. */
void Device::WaitIdleNoLock() {
	// Flush all pending submissions, if we have any.
	if (!_frameContexts.empty()) { EndFrameNoLock(); }

	if (_device) {
		// Await the completion of all GPU work.
		_device.waitIdle();

		_framebuffers.Clear();
		_transientAttachments.Clear();

		for (auto& queue : _queueData) {
			for (auto& semaphore : queue.WaitSemaphores) { _device.destroySemaphore(semaphore->Release()); }
			queue.WaitSemaphores.clear();
			queue.WaitStages.clear();
		}

		for (auto& frame : _frameContexts) {
			frame->FencesToAwait.clear();
			frame->Begin();
		}
	}

	// Now that the device is idle, we can guarantee that all pending work has been completed.
	// Therefore, we can remove the need to wait on any Semaphores or Fences.

	// First, we will remove all pending wait semaphores from our queues.
	for (auto& queue : _queueData) {
		for (auto& semaphore : queue.WaitSemaphores) { _device.destroySemaphore(semaphore->Consume()); }
		queue.WaitSemaphores.clear();
		queue.WaitStages.clear();
	}

	// Now, we remove all pending Fence waits from our frame contexts.
	// We can also use this time to clear our all of their deletion queues, as none of the objects are in use anymore.
	for (auto& context : _frameContexts) {
		context->FencesToAwait.clear();  // Remove all pending Fence waits.
		context->Begin();                // Destroy all objects pending destruction.
		context->Trim();                 // Trim Command Pools to optimize memory usage.
	}
}

/* ====================================
** ===== Private Helper Functions =====
*  ==================================== */
uint64_t Device::AllocateCookie() {
	return _nextCookie.fetch_add(16, std::memory_order_relaxed) + 16;
}

void Device::CreateFrameContexts(uint32_t count) {
	DeviceFlush();
	WaitIdleNoLock();

	_frameContexts.clear();
	for (uint32_t i = 0; i < count; ++i) { _frameContexts.emplace_back(new FrameContext(*this, i)); }
}

double Device::ConvertDeviceTimestampDelta(uint64_t startTicks, uint64_t endTicks) const {
	int64_t ticksDelta = ConvertToSignedDelta(startTicks, endTicks, _queueInfo.TimestampValidBits);

	return double(int64_t(ticksDelta)) * _deviceInfo.Properties.Core.limits.timestampPeriod * 1e-9;
}

/** Return the current frame context. */
Device::FrameContext& Device::Frame() {
	return *_frameContexts[_currentFrameContext];
}

vk::Format Device::GetDefaultDepthFormat() const {
	if (IsFormatSupported(
				vk::Format::eD32Sfloat, vk::FormatFeatureFlagBits::eDepthStencilAttachment, vk::ImageTiling::eOptimal)) {
		return vk::Format::eD32Sfloat;
	}
	if (IsFormatSupported(
				vk::Format::eX8D24UnormPack32, vk::FormatFeatureFlagBits::eDepthStencilAttachment, vk::ImageTiling::eOptimal)) {
		return vk::Format::eX8D24UnormPack32;
	}
	if (IsFormatSupported(
				vk::Format::eD16Unorm, vk::FormatFeatureFlagBits::eDepthStencilAttachment, vk::ImageTiling::eOptimal)) {
		return vk::Format::eD16Unorm;
	}

	return vk::Format::eUndefined;
}

vk::Format Device::GetDefaultDepthStencilFormat() const {
	if (IsFormatSupported(
				vk::Format::eD24UnormS8Uint, vk::FormatFeatureFlagBits::eDepthStencilAttachment, vk::ImageTiling::eOptimal)) {
		return vk::Format::eD24UnormS8Uint;
	}
	if (IsFormatSupported(
				vk::Format::eD32SfloatS8Uint, vk::FormatFeatureFlagBits::eDepthStencilAttachment, vk::ImageTiling::eOptimal)) {
		return vk::Format::eD32SfloatS8Uint;
	}

	return vk::Format::eUndefined;
}

/** Return the Queue type that should be responsible for executing this command buffer type. */
QueueType Device::GetQueueType(CommandBufferType type) const {
	if (type != CommandBufferType::AsyncGraphics) { return static_cast<QueueType>(type); }

	if (_queueInfo.SameFamily(QueueType::Graphics, QueueType::Compute) &&
	    !_queueInfo.SameQueue(QueueType::Graphics, QueueType::Compute)) {
		return QueueType::Compute;
	}

	return QueueType::Graphics;
}

bool Device::IsFormatSupported(vk::Format format, vk::FormatFeatureFlags features, vk::ImageTiling tiling) const {
	const auto props = _deviceInfo.PhysicalDevice.getFormatProperties(format);
	const auto featureFlags =
		tiling == vk::ImageTiling::eOptimal ? props.optimalTilingFeatures : props.linearTilingFeatures;

	return (featureFlags & features) == features;
}

/* ==================================
** ===== FrameContext Functions =====
*  ================================== */
Device::FrameContext::FrameContext(Device& parent, uint32_t frameIndex)
		: Parent(parent), FrameIndex(frameIndex), QueryPool(Parent) {
	const auto threadCount = Threading::GetThreadCount() + 1;
	for (int type = 0; type < QueueTypeCount; ++type) {
		TimelineValues[type] = Parent._queueData[type].TimelineValue;

		CommandPools[type].reserve(threadCount);
		for (int i = 0; i < threadCount; ++i) {
			CommandPools[type].emplace_back(
				Parent, Parent._queueInfo.Families[type], std::format("{} Command Pool - Thread {}", QueueType(type), i));
		}
	}
}

Device::FrameContext::~FrameContext() noexcept {
	// Ensure our deletion queue is empty before we're fully destroyed.
	Begin();
}

void Device::FrameContext::Begin() {
	auto device = Parent._device;

	// First ensure whether we have Timeline Semaphores. Otherwise we need to use normal Fences.
	bool hasTimelineSemaphores = true;
	for (const auto& queue : Parent._queueData) {
		if (!queue.TimelineSemaphore) {
			hasTimelineSemaphores = false;
			break;
		}
	}

	// Await all timeline semaphores, if applicable.
	if (hasTimelineSemaphores) {
		// First gather all semaphores that don't have a 0 value.
		int semaphoreCount = 0;
		std::array<vk::Semaphore, QueueTypeCount> semaphores;
		std::array<uint64_t, QueueTypeCount> timelineValues;
		for (int i = 0; i < QueueTypeCount; ++i) {
			if (TimelineValues[i]) {
				semaphores[semaphoreCount]     = Parent._queueData[i].TimelineSemaphore;
				timelineValues[semaphoreCount] = TimelineValues[i];
				++semaphoreCount;
			}
		}

		// If any semaphores were found, wait on all of them at once.
		if (semaphoreCount) {
			const vk::SemaphoreWaitInfo waitInfo({}, semaphoreCount, semaphores.data(), timelineValues.data());
			const auto waitResult = device.waitSemaphores(waitInfo, std::numeric_limits<uint64_t>::max());
			if (waitResult != vk::Result::eSuccess) {
				Log::Error("Vulkan", "Failed to wait on semaphores: {}", vk::to_string(waitResult));
			}
		}
	}

	// Await all pending Fences.
	if (!FencesToAwait.empty()) {
		const auto waitResult = device.waitForFences(FencesToAwait, VK_TRUE, std::numeric_limits<uint64_t>::max());
		if (waitResult != vk::Result::eSuccess) { Log::Error("Vulkan", "Failed to wait on Fences"); }
		FencesToAwait.clear();
	}
	if (!FencesToRecycle.empty()) {
		device.resetFences(FencesToRecycle);
		for (auto& fence : FencesToRecycle) { Parent.FreeFence(fence); }
		FencesToRecycle.clear();
	}

	// Start all of our command pools.
	for (auto& queuePools : CommandPools) {
		for (auto& pool : queuePools) { pool.Begin(); }
	}
	QueryPool.Begin();

	// Clean up all deferred object deletions.
	for (auto buffer : BuffersToDestroy) { device.destroyBuffer(buffer); }
	for (auto framebuffer : FramebuffersToDestroy) { device.destroyFramebuffer(framebuffer); }
	for (auto image : ImagesToDestroy) { device.destroyImage(image); }
	for (auto imageView : ImageViewsToDestroy) { device.destroyImageView(imageView); }
	for (auto sampler : SamplersToDestroy) { device.destroySampler(sampler); }
	for (auto semaphore : SemaphoresToDestroy) { device.destroySemaphore(semaphore); }
	for (auto semaphore : SemaphoresToRecycle) { Parent.FreeSemaphore(semaphore); }
	BuffersToDestroy.clear();
	FramebuffersToDestroy.clear();
	ImagesToDestroy.clear();
	ImageViewsToDestroy.clear();
	SamplersToDestroy.clear();
	SemaphoresToDestroy.clear();
	SemaphoresToRecycle.clear();

	if (!AllocationsToFree.empty() || !AllocationsToUnmap.empty()) {
		std::lock_guard<std::mutex> lock(Parent._lock.MemoryLock);
		for (auto allocation : AllocationsToUnmap) { vmaUnmapMemory(Parent._allocator, allocation); }
		for (auto allocation : AllocationsToFree) { vmaFreeMemory(Parent._allocator, allocation); }
	}
	AllocationsToFree.clear();
	AllocationsToUnmap.clear();

	Log::Assert(SemaphoresToConsume.empty(), "Vulkan", "Not all semaphores were consumed");

	for (auto& ts : TimestampIntervals) {
		if (ts.Start->IsSignalled() && ts.End->IsSignalled()) {
			const auto startTs = ts.Start->GetTimestampTicks();
			const auto endTs   = ts.End->GetTimestampTicks();
			ts.TimestampTag->Reset();
			if (ts.Start->IsDeviceTimebase()) {
				ts.TimestampTag->AccumulateTime(Parent.ConvertDeviceTimestampDelta(startTs, endTs));
			} else {
				ts.TimestampTag->AccumulateTime(1e-9 * double(endTs - startTs));
			}
		}
	}
	TimestampIntervals.clear();
}

void Device::FrameContext::Trim() {}
}  // namespace Vulkan
}  // namespace Luna
