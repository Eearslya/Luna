#pragma once

#include <Luna/Vulkan/Common.hpp>

namespace Luna {
namespace Vulkan {
class Swapchain {
 public:
	Swapchain(Device& device);
	~Swapchain() noexcept;

	uint32_t GetAcquiredIndex() const {
		return _acquiredImage;
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

	bool AcquireNextImage();
	void Present();

 private:
	void RecreateSwapchain();

	Device& _device;
	vk::SwapchainKHR _swapchain;
	vk::Extent2D _extent;
	vk::SurfaceFormatKHR _format;
	uint32_t _imageCount = 0;
	std::vector<vk::Image> _images;
	vk::PresentModeKHR _presentMode;
	std::vector<SemaphoreHandle> _releaseSemaphores;
	bool _suboptimal = false;

	uint32_t _acquiredImage = std::numeric_limits<uint32_t>::max();
};
}  // namespace Vulkan
}  // namespace Luna
