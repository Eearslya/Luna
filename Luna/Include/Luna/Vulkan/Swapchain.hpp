#pragma once

#include <Luna/Vulkan/Common.hpp>

namespace Luna {
namespace Vulkan {
class Swapchain {
 public:
	Swapchain(Device& device);
	~Swapchain() noexcept;

	uint32_t AcquireNextImage();
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
