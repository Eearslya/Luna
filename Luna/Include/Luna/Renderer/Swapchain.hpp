#pragma once

#include <Luna/Vulkan/Common.hpp>

namespace Luna {
class Window;
class Swapchain : public IntrusivePtrEnabled<Swapchain> {
	friend class Vulkan::Device;

 public:
	Swapchain(Window& window);
	~Swapchain() noexcept;

	[[nodiscard]] vk::ColorSpaceKHR GetColorSpace() const noexcept {
		return _format.colorSpace;
	}
	[[nodiscard]] const vk::Extent2D GetExtent() const noexcept {
		return _extent;
	}
	[[nodiscard]] vk::Format GetFormat() const noexcept {
		return _format.format;
	}
	[[nodiscard]] Hash GetHash() const noexcept {
		return _swapchainHash;
	}
	[[nodiscard]] uint32_t GetImageCount() const noexcept {
		return uint32_t(_images.size());
	}
	[[nodiscard]] vk::PresentModeKHR GetPresentMode() const noexcept {
		return _presentMode;
	}
	[[nodiscard]] const vk::SurfaceFormatKHR& GetSurfaceFormat() const noexcept {
		return _format;
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

	Hash _swapchainHash;
};
}  // namespace Luna
