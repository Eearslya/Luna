#pragma once

#include "Common.hpp"

namespace Luna {
namespace Vulkan {
class WSIPlatform {
 public:
	virtual ~WSIPlatform() noexcept = default;

	virtual vk::SurfaceKHR CreateSurface(vk::Instance instance, vk::PhysicalDevice gpu) = 0;
	virtual void DestroySurface(vk::Instance instance, vk::SurfaceKHR surface)          = 0;
	virtual std::vector<const char*> GetInstanceExtensions()                            = 0;
	virtual std::vector<const char*> GetDeviceExtensions()                              = 0;
	virtual uint32_t GetSurfaceHeight()                                                 = 0;
	virtual uint32_t GetSurfaceWidth()                                                  = 0;
	virtual bool IsAlive()                                                              = 0;
	virtual void Update()                                                               = 0;
};

class WSI {
 public:
	WSI(std::unique_ptr<WSIPlatform>&& platform, bool srgb = true);
	~WSI() noexcept;

	uint32_t GetAcquiredIndex() const {
		return _acquiredImage;
	}
	Context& GetContext() {
		return *_context;
	}
	Device& GetDevice() {
		return *_device;
	}
	const vk::Extent2D& GetExtent() const {
		return _extent;
	}
	vk::Format GetFormat() const {
		return _format.format;
	}
	const std::vector<vk::Image>& GetImages() const {
		return _images;
	}
	vk::Image GetImage(uint32_t index) const {
		return _images[index];
	}
	bool IsAlive() {
		return _platform->IsAlive();
	}

	void BeginFrame();
	void EndFrame();

 private:
	void RecreateSwapchain();

	std::unique_ptr<WSIPlatform> _platform;
	ContextHandle _context;
	DeviceHandle _device;
	vk::SurfaceKHR _surface;

	uint32_t _acquiredImage = std::numeric_limits<uint32_t>::max();
	vk::SwapchainKHR _swapchain;
	vk::Extent2D _extent;
	vk::SurfaceFormatKHR _format;
	uint32_t _imageCount = 0;
	std::vector<vk::Image> _images;
	vk::PresentModeKHR _presentMode;
	std::vector<SemaphoreHandle> _releaseSemaphores;
	bool _suboptimal = false;
};
}  // namespace Vulkan
}  // namespace Luna
