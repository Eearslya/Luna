#pragma once

#include <Luna/Utility/Delegate.hpp>
#include <Luna/Utility/IntrusivePtr.hpp>
#include <Luna/Vulkan/Common.hpp>
#include <glm/glm.hpp>
#include <memory>
#include <vector>

namespace Luna {
namespace Vulkan {
class WSIPlatform {
 public:
	virtual ~WSIPlatform() noexcept = default;

	virtual glm::uvec2 GetFramebufferSize() const                          = 0;
	virtual std::vector<const char*> GetRequiredDeviceExtensions() const   = 0;
	virtual std::vector<const char*> GetRequiredInstanceExtensions() const = 0;
	virtual bool IsAlive() const                                           = 0;

	virtual vk::SurfaceKHR CreateSurface(vk::Instance instance) = 0;
	virtual void Update()                                       = 0;

 protected:
 private:
};

class WSI {
	friend class Device;

 public:
	WSI(WSIPlatform* platform);
	~WSI() noexcept;

	Vulkan::Device& GetDevice() {
		return *_device;
	}

	void BeginFrame();
	void EndFrame();
	bool IsAlive();
	void Update();

	Delegate<void(const SwapchainConfiguration&)> OnSwapchainChanged;

 private:
	void RecreateSwapchain();

	std::unique_ptr<WSIPlatform> _platform;
	IntrusivePtr<Context> _context;
	IntrusivePtr<Device> _device;
	vk::SurfaceKHR _surface;

	vk::SwapchainKHR _swapchain;
	uint32_t _swapchainAcquired = std::numeric_limits<uint32_t>::max();
	SwapchainConfiguration _swapchainConfig;
	std::vector<vk::Image> _swapchainImages;
	std::vector<SemaphoreHandle> _swapchainRelease;
	bool _swapchainSuboptimal = false;
};
}  // namespace Vulkan
}  // namespace Luna
