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

	Device(const Context& context);
	Device(const Device&)            = delete;
	Device& operator=(const Device&) = delete;
	~Device() noexcept;

	BufferHandle CreateBuffer(const BufferCreateInfo& bufferCI);

	uint64_t AllocateCookie();
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

		std::vector<vk::Buffer> BuffersToDestroy;
		std::vector<VmaAllocation> MemoryToFree;
	};

	FrameContext& Frame();

	void DestroyBuffer(vk::Buffer buffer);
	void FreeMemory(const VmaAllocation& allocation);

	void DestroyBufferNoLock(vk::Buffer buffer);
	void FreeMemoryNoLock(const VmaAllocation& allocation);

	void WaitIdleNoLock();

	const ExtensionInfo _extensions;
	const vk::Instance _instance;
	const GPUInfo _gpuInfo;
	const QueueInfo _queues;
	const vk::PhysicalDevice _gpu;
	const vk::Device _device;

	std::vector<std::unique_ptr<FrameContext>> _frameContexts;
	uint32_t _currentFrameContext = 0;

	VmaAllocator _allocator;

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
};
}  // namespace Vulkan
}  // namespace Luna
