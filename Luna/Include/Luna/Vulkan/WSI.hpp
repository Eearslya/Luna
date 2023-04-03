#pragma once

#include <Luna/Application/Input.hpp>
#include <Luna/Utility/Delegate.hpp>
#include <Luna/Utility/Timer.hpp>
#include <Luna/Vulkan/Common.hpp>
#include <glm/glm.hpp>
#include <vector>

namespace Luna {
namespace Vulkan {
class Context;

class WSIPlatform {
 public:
	virtual ~WSIPlatform() noexcept = default;

	virtual InputAction GetButton(MouseButton) const                       = 0;
	virtual glm::uvec2 GetFramebufferSize() const                          = 0;
	virtual InputAction GetKey(Key) const                                  = 0;
	virtual std::vector<const char*> GetRequiredDeviceExtensions() const   = 0;
	virtual std::vector<const char*> GetRequiredInstanceExtensions() const = 0;
	virtual double GetTime() const                                         = 0;
	virtual glm::uvec2 GetWindowSize() const                               = 0;
	virtual bool IsAlive() const                                           = 0;

	virtual VkSurfaceKHR CreateSurface(VkInstance instance) = 0;
	virtual void Initialize()                               = 0;
	virtual void Update()                                   = 0;
	virtual void Shutdown()                                 = 0;

	FrameTimer& GetFrameTimer() {
		return _timer;
	}

 private:
	FrameTimer _timer;
};

class WSI {
	friend class Device;

 public:
	WSI(WSIPlatform* platform);
	~WSI() noexcept;

	Vulkan::Device& GetDevice() {
		return *_device;
	}
	double GetSmoothElapsedTime() const {
		return _smoothElapsedTime;
	}
	double GetSmoothFrameTime() const {
		return _smoothFrameTime;
	}
	const SwapchainConfiguration& GetSwapchainConfig() const {
		return _swapchainConfig;
	}

	InputAction GetButton(MouseButton) const;
	glm::uvec2 GetFramebufferSize() const;
	InputAction GetKey(Key) const;
	double GetTime() const;
	glm::uvec2 GetWindowSize() const;

	void BeginFrame();
	void EndFrame();
	bool IsAlive();
	void Update();

	Delegate<void(const SwapchainConfiguration&)> OnSwapchainChanged;

 private:
	static constexpr uint32_t NotAcquired = std::numeric_limits<uint32_t>::max();

	void RecreateSwapchain();

	WSIPlatform* _platform = nullptr;
	ContextHandle _context;
	DeviceHandle _device;
	vk::SurfaceKHR _surface;

	vk::SwapchainKHR _swapchain;
	uint32_t _swapchainAcquired = NotAcquired;
	SwapchainConfiguration _swapchainConfig;
	std::vector<vk::Image> _swapchainImages;
	std::vector<SemaphoreHandle> _swapchainRelease;
	bool _swapchainSuboptimal = false;

	double _smoothElapsedTime = 0.0;
	double _smoothFrameTime   = 0.0;
};
}  // namespace Vulkan
}  // namespace Luna
