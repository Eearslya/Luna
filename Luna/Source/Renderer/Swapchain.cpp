#include <Luna/Core/Window.hpp>
#include <Luna/Renderer/Renderer.hpp>
#include <Luna/Renderer/Swapchain.hpp>
#include <Luna/Vulkan/Device.hpp>
#include <Luna/Vulkan/Semaphore.hpp>

namespace Luna {
Swapchain::Swapchain(Window& window) : _window(&window) {
	const auto& device = Renderer::GetDevice();
	_surface           = window.CreateSurface(device.GetInstance());

	const auto& deviceInfo  = device.GetDeviceInfo();
	const auto capabilities = deviceInfo.PhysicalDevice.getSurfaceCapabilitiesKHR(_surface);
	const auto formats      = deviceInfo.PhysicalDevice.getSurfaceFormatsKHR(_surface);
	const auto presentModes = deviceInfo.PhysicalDevice.getSurfacePresentModesKHR(_surface);

	_format.format = vk::Format::eUndefined;
	for (const auto& format : formats) {
		if (format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear &&
		    (format.format == vk::Format::eR8G8B8A8Srgb || format.format == vk::Format::eB8G8R8A8Srgb ||
		     format.format == vk::Format::eA8B8G8R8SrgbPack32)) {
			_format = format;
			break;
		}
	}
	if (_format.format == vk::Format::eUndefined) { _format = formats[0]; }

	_presentMode = vk::PresentModeKHR::eFifo;
	for (const auto& presentMode : presentModes) {
		if (presentMode == vk::PresentModeKHR::eMailbox) {
			_presentMode = presentMode;
			break;
		}
	}
}

Swapchain::~Swapchain() noexcept {
	auto& device = Renderer::GetDevice();

	_release.clear();
	device.SetAcquireSemaphore(0, Vulkan::SemaphoreHandle{});
	device.ConsumeReleaseSemaphore();
	device.WaitIdle();

	if (_swapchain) { device.GetDevice().destroySwapchainKHR(_swapchain); }
	if (_surface) { device.GetInstance().destroySurfaceKHR(_surface); }
}

bool Swapchain::Acquire() {
	if (!_swapchain || _suboptimal) { Recreate(); }
	if (_acquired != NotAcquired) { return true; }

	auto device = Renderer::GetDevice().GetDevice();

	int retry = 0;
	while (retry < 3) {
		auto acquire = Renderer::GetDevice().RequestSemaphore("Swapchain Acquire");

		try {
			const auto acquireResult =
				device.acquireNextImageKHR(_swapchain, std::numeric_limits<uint64_t>::max(), acquire->GetSemaphore(), nullptr);

			if (acquireResult.result == vk::Result::eSuboptimalKHR) { _suboptimal = true; }

			acquire->SignalExternal();
			acquire->SetForeignQueue();
			_acquired = acquireResult.value;
			_release[_acquired].Reset();
			Renderer::GetDevice().SetAcquireSemaphore(_acquired, acquire);

			break;
		} catch (const vk::OutOfDateKHRError& e) {
			Recreate();
			++retry;

			continue;
		}
	}

	return _acquired != NotAcquired;
}

void Swapchain::Present() {
	if (_acquired == NotAcquired) { return; }

	const auto& queues = Renderer::GetDevice().GetQueueInfo();
	auto queue         = queues.Queue(Vulkan::QueueType::Graphics);

	Renderer::GetDevice().EndFrame();
	if (!Renderer::GetDevice().SwapchainAcquired()) { return; }

	auto release          = Renderer::GetDevice().ConsumeReleaseSemaphore();
	auto releaseSemaphore = release->GetSemaphore();
	const vk::PresentInfoKHR presentInfo(releaseSemaphore, _swapchain, _acquired);
	vk::Result presentResult = vk::Result::eSuccess;
	try {
		presentResult = queue.presentKHR(presentInfo);
		if (presentResult == vk::Result::eSuboptimalKHR) { _suboptimal = true; }
		release->WaitExternal();
		_release[_acquired] = std::move(release);
	} catch (const vk::OutOfDateKHRError& e) { Recreate(); }

	_acquired = NotAcquired;
}

void Swapchain::Recreate() {
	auto physicalDevice     = Renderer::GetDevice().GetDeviceInfo().PhysicalDevice;
	auto device             = Renderer::GetDevice().GetDevice();
	const auto capabilities = physicalDevice.getSurfaceCapabilitiesKHR(_surface);
	const auto fbSize       = _window->GetFramebufferSize();

	if (fbSize.x == 0 || fbSize.y == 0 || capabilities.maxImageExtent.width == 0 ||
	    capabilities.maxImageExtent.height == 0) {
		return;
	}

	_extent = vk::Extent2D(
		glm::clamp<uint32_t>(fbSize.x, capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
		glm::clamp<uint32_t>(fbSize.y, capabilities.minImageExtent.height, capabilities.maxImageExtent.height));

	uint32_t imageCount = glm::max(3u, capabilities.minImageCount);
	if (capabilities.maxImageCount > 0) { imageCount = glm::min(imageCount, capabilities.maxImageCount); }

	const vk::SwapchainCreateInfoKHR swapchainCI(
		{},
		_surface,
		imageCount,
		_format.format,
		_format.colorSpace,
		_extent,
		1,
		vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferDst,
		vk::SharingMode::eExclusive,
		nullptr,
		vk::SurfaceTransformFlagBitsKHR::eIdentity,
		vk::CompositeAlphaFlagBitsKHR::eOpaque,
		_presentMode,
		VK_TRUE,
		_swapchain);
	auto newSwapchain = device.createSwapchainKHR(swapchainCI);

	if (_swapchain) { device.destroySwapchainKHR(_swapchain); }
	_swapchain = newSwapchain;

	_acquired = NotAcquired;
	_images   = device.getSwapchainImagesKHR(_swapchain);
	_release.clear();
	_release.resize(_images.size());
	_suboptimal = false;

	Renderer::GetDevice().SetupSwapchain(_extent, _format, _images);
}
}  // namespace Luna
