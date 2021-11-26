#pragma once

#include <Luna/Vulkan/Common.hpp>

namespace Luna {
namespace Vulkan {
class Context;

class Device final : NonCopyable {
	friend class CommandPool;

 public:
	Device(const Context& context);
	~Device() noexcept;

	// Frame management.
	void NextFrame();

	// General functionality.
	void WaitIdle();

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
	};

	// Frame management.
	FrameContext& Frame();

	// General functionality.
	void WaitIdleNoLock();

	// Internal setup and cleanup.
	void CreateFrameContexts(uint32_t count);

	// All of our Vulkan information/objects inherited from Context.
	const ExtensionInfo& _extensions;
	const vk::Instance& _instance;
	const vk::SurfaceKHR& _surface;
	const GPUInfo& _gpuInfo;
	const QueueInfo& _queues;
	const vk::PhysicalDevice& _gpu;
	const vk::Device& _device;

	// Multithreading synchronization objects.
#ifdef LUNA_VULKAN_MT
	std::mutex _mutex;
	std::condition_variable _pendingCommandBuffersCondition;
#endif
	uint32_t _pendingCommandBuffers = 0;

	// Frame contexts.
	uint32_t _currentFrameContext = 0;
	std::vector<std::unique_ptr<FrameContext>> _frameContexts;
};
}  // namespace Vulkan
}  // namespace Luna
