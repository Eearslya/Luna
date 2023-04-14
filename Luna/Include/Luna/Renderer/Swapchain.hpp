#pragma once

#include <Luna/Vulkan/Common.hpp>

namespace Luna {
class Window;

class Swapchain : public IntrusivePtrEnabled<Swapchain> {
	friend class Vulkan::Device;

 public:
	Swapchain(Window& window);
	Swapchain(const Swapchain&)      = delete;
	void operator=(const Swapchain&) = delete;
	~Swapchain() noexcept;

	const vk::Extent2D& GetExtent() const {
		return _extent;
	}
	const vk::Format GetFormat() const {
		return _format.format;
	}

	bool Acquire();
	void Present();

 private:
	static constexpr uint32_t NotAcquired = std::numeric_limits<uint32_t>::max();

	void Recreate();

	Window* _window = nullptr;
	vk::SurfaceKHR _surface;
	vk::SwapchainKHR _swapchain;
	vk::Extent2D _extent;
	vk::SurfaceFormatKHR _format;
	vk::PresentModeKHR _presentMode;
	uint32_t _acquired = NotAcquired;
	std::vector<vk::Image> _images;
	std::vector<Vulkan::SemaphoreHandle> _release;
	bool _suboptimal = false;
};
}  // namespace Luna
