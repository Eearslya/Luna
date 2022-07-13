#include "Device.hpp"

#include "Buffer.hpp"
#include "CommandBuffer.hpp"
#include "CommandPool.hpp"
#include "Context.hpp"
#include "Utility/Log.hpp"

#ifdef LUNA_VULKAN_MT
static uint32_t GetThreadIndex() {
	return 0;
}
#	define DeviceLock() std::lock_guard<std::mutex> lock(_lock.Mutex)
#	define DeviceFlush()                                                 \
		do {                                                                \
			std::unique_lock<std::mutex> lock(_lock.Mutex);                   \
			_lock.Condition.wait(lock, [&]() { return _lock.Counter == 0; }); \
		} while (0)
#else
static uint32_t GetThreadIndex() {
	return 0;
}
#	define DeviceLock()  ((void) 0)
#	define DeviceFlush() assert(_lock.Counter == 0)
#endif

namespace Luna {
namespace Vulkan {
Device::Device(const Context& context)
		: _extensions(context.GetExtensionInfo()),
			_instance(context.GetInstance()),
			_gpuInfo(context.GetGPUInfo()),
			_queues(context.GetQueueInfo()),
			_gpu(context.GetGPU()),
			_device(context.GetDevice()) {
#ifdef LUNA_VULKAN_MT
	_cookie.store(0);
#endif

	// Initialize our VMA allocator.
	{
#define FN(name) .name = VULKAN_HPP_DEFAULT_DISPATCHER.name
		VmaVulkanFunctions vmaFunctions = {FN(vkGetInstanceProcAddr),
		                                   FN(vkGetDeviceProcAddr),
		                                   FN(vkGetPhysicalDeviceProperties),
		                                   FN(vkGetPhysicalDeviceMemoryProperties),
		                                   FN(vkAllocateMemory),
		                                   FN(vkFreeMemory),
		                                   FN(vkMapMemory),
		                                   FN(vkUnmapMemory),
		                                   FN(vkFlushMappedMemoryRanges),
		                                   FN(vkInvalidateMappedMemoryRanges),
		                                   FN(vkBindBufferMemory),
		                                   FN(vkBindImageMemory),
		                                   FN(vkGetBufferMemoryRequirements),
		                                   FN(vkGetImageMemoryRequirements),
		                                   FN(vkCreateBuffer),
		                                   FN(vkDestroyBuffer),
		                                   FN(vkCreateImage),
		                                   FN(vkDestroyImage),
		                                   FN(vkCmdCopyBuffer)};
#undef FN
#define FN(core) vmaFunctions.core##KHR = reinterpret_cast<PFN_##core##KHR>(VULKAN_HPP_DEFAULT_DISPATCHER.core)
		FN(vkGetBufferMemoryRequirements2);
		FN(vkGetImageMemoryRequirements2);
		FN(vkBindBufferMemory2);
		FN(vkBindImageMemory2);
		FN(vkGetPhysicalDeviceMemoryProperties2);
#undef FN
		if (_extensions.Maintenance4) {
			vmaFunctions.vkGetDeviceBufferMemoryRequirements =
				VULKAN_HPP_DEFAULT_DISPATCHER.vkGetDeviceBufferMemoryRequirementsKHR;
			vmaFunctions.vkGetDeviceImageMemoryRequirements =
				VULKAN_HPP_DEFAULT_DISPATCHER.vkGetDeviceImageMemoryRequirementsKHR;
		}

		const VmaAllocatorCreateInfo allocatorCI = {.physicalDevice   = _gpu,
		                                            .device           = _device,
		                                            .pVulkanFunctions = &vmaFunctions,
		                                            .instance         = _instance,
		                                            .vulkanApiVersion = VK_API_VERSION_1_1};
		const auto allocatorResult               = vmaCreateAllocator(&allocatorCI, &_allocator);
		if (allocatorResult != VK_SUCCESS) {
			throw std::runtime_error("[Vulkan::Device] Failed to create memory allocator!");
		}
	}

	// Create our frame contexts.
	{
		DeviceFlush();
		WaitIdleNoLock();

		_frameContexts.clear();
		for (int i = 0; i < 2; ++i) {
			auto frame = std::make_unique<FrameContext>(*this, i);
			_frameContexts.emplace_back(std::move(frame));
		}
	}
}

Device::~Device() noexcept {
	WaitIdle();

	vmaDestroyAllocator(_allocator);
}

BufferHandle Device::CreateBuffer(const BufferCreateInfo& createInfo) {
	BufferCreateInfo actualCI = createInfo;
	actualCI.Usage |= vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eTransferSrc;

	auto queueFamilies = _queues.UniqueFamilies();

	VmaAllocationCreateFlags allocFlags = {};
	if (actualCI.Domain == BufferDomain::Host) {
		allocFlags |= VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
	}

	const vk::BufferCreateInfo bufferCI(
		{},
		actualCI.Size,
		actualCI.Usage,
		queueFamilies.size() == 1 ? vk::SharingMode::eExclusive : vk::SharingMode::eConcurrent,
		queueFamilies);
	const VmaAllocationCreateInfo bufferAI = {.flags         = allocFlags,
	                                          .usage         = VMA_MEMORY_USAGE_AUTO,
	                                          .requiredFlags = actualCI.Domain == BufferDomain::Host
	                                                             ? VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
	                                                             : VkMemoryPropertyFlags{}};

	VkBuffer buffer;
	VmaAllocation allocation;
	VmaAllocationInfo allocationInfo;
	const auto result = vmaCreateBuffer(_allocator,
	                                    reinterpret_cast<const VkBufferCreateInfo*>(&bufferCI),
	                                    &bufferAI,
	                                    &buffer,
	                                    &allocation,
	                                    &allocationInfo);
	if (result != VK_SUCCESS) {
		Log::Error("Vulkan", "Failed to create buffer: {}", vk::to_string(vk::Result(result)));
		return BufferHandle();
	}

	const bool mappable(_gpuInfo.Memory.memoryTypes[allocationInfo.memoryType].propertyFlags &
	                    vk::MemoryPropertyFlagBits::eHostVisible);
	void* mappedMemory = allocationInfo.pMappedData;
	if (mappable && !mappedMemory) { vmaMapMemory(_allocator, allocation, &mappedMemory); }

	BufferHandle handle(_bufferPool.Allocate(*this, buffer, allocation, actualCI, mappedMemory));

	return handle;
}

CommandBufferHandle Device::RequestCommandBuffer(CommandBufferType type) {
	return RequestCommandBufferForThread(GetThreadIndex(), type);
}

CommandBufferHandle Device::RequestCommandBufferForThread(uint32_t threadIndex, CommandBufferType type) {
	DeviceLock();
	return RequestCommandBufferNoLock(threadIndex, type);
}

uint64_t Device::AllocateCookie() {
#ifdef LUNA_VULKAN_MT
	return _cookie.fetch_add(16, std::memory_order_relaxed) + 16;
#else
	_cookie += 16;
	return _cookie;
#endif
}

void Device::WaitIdle() {
	DeviceFlush();
	WaitIdleNoLock();
}

Device::FrameContext& Device::Frame() {
	return *_frameContexts[_currentFrameContext];
}

QueueType Device::GetQueueType(CommandBufferType cbType) const {
	if (cbType != CommandBufferType::AsyncGraphics) {
		return static_cast<QueueType>(cbType);
	} else {
		if (_queues.SameFamily(QueueType::Graphics, QueueType::Compute) &&
		    !_queues.SameQueue(QueueType::Graphics, QueueType::Compute)) {
			return QueueType::Compute;
		} else {
			return QueueType::Graphics;
		}
	}
}

void Device::DestroyBuffer(vk::Buffer buffer) {
	DeviceLock();
	DestroyBufferNoLock(buffer);
}

void Device::FreeMemory(const VmaAllocation& allocation) {
	DeviceLock();
	FreeMemoryNoLock(allocation);
}

void Device::DestroyBufferNoLock(vk::Buffer buffer) {
	Frame().BuffersToDestroy.push_back(buffer);
}

void Device::FreeMemoryNoLock(const VmaAllocation& allocation) {
	Frame().MemoryToFree.push_back(allocation);
}

CommandBufferHandle Device::RequestCommandBufferNoLock(uint32_t threadIndex, CommandBufferType type) {
	const auto queueType = GetQueueType(type);
	auto& pool           = Frame().CommandPools[int(queueType)][threadIndex];
	auto cmd             = pool->RequestCommandBuffer();

	const vk::CommandBufferBeginInfo cmdBI(vk::CommandBufferUsageFlagBits::eOneTimeSubmit, nullptr);
	cmd.begin(cmdBI);
	++_lock.Counter;

	CommandBufferHandle handle(_commandBufferPool.Allocate(*this, cmd, type, threadIndex));

	return handle;
}

void Device::WaitIdleNoLock() {
	_device.waitIdle();

	for (auto& frame : _frameContexts) { frame->Begin(); }
}

Device::FrameContext::FrameContext(Device& device, uint32_t index) : Parent(device), Index(index) {
	const auto threadCount = 1;
	for (int i = 0; i < QueueTypeCount; ++i) {
		CommandPools[i].reserve(threadCount);
		for (int j = 0; j < threadCount; ++j) {
			CommandPools[i].emplace_back(std::make_unique<CommandPool>(Parent, Parent._queues.Families[i], false));
		}
	}
}

Device::FrameContext::~FrameContext() noexcept {
	Begin();
}

void Device::FrameContext::Begin() {
	auto device = Parent._device;

	for (auto& pools : CommandPools) {
		for (auto& pool : pools) { pool->Reset(); }
	}

	for (auto& buffer : BuffersToDestroy) { device.destroyBuffer(buffer); }
	for (auto& allocation : MemoryToFree) { vmaFreeMemory(Parent._allocator, allocation); }
	BuffersToDestroy.clear();
	MemoryToFree.clear();
}
}  // namespace Vulkan
}  // namespace Luna
