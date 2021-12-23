#include <Luna/Core/Log.hpp>
#include <Luna/Devices/Window.hpp>
#include <Luna/Vulkan/Device.hpp>
#include <Luna/Vulkan/Semaphore.hpp>
#include <Luna/Vulkan/Swapchain.hpp>

namespace Luna {
namespace Vulkan {
Swapchain::Swapchain(Device& device) : _device(device) {
	auto gpu     = _device.GetGPU();
	auto dev     = _device.GetDevice();
	auto surface = _device.GetSurface();

	auto formats      = gpu.getSurfaceFormatsKHR(surface);
	auto presentModes = gpu.getSurfacePresentModesKHR(surface);

	_format.format = vk::Format::eUndefined;
	for (auto& format : formats) {
		if (format.format == vk::Format::eB8G8R8A8Srgb && format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear) {
			_format = format;
			break;
		}
	}
	if (_format.format == vk::Format::eUndefined) { _format = formats[0]; }

	_presentMode = vk::PresentModeKHR::eFifo;

	Log::Trace(
		"[Vulkan::Swapchain] Swapchain Format: {}, {}", vk::to_string(_format.format), vk::to_string(_format.colorSpace));
	Log::Trace("[Vulkan::Swapchain] Swapchain Present Mode: {}", vk::to_string(_presentMode));

	RecreateSwapchain();
}

Swapchain::~Swapchain() noexcept {
	auto device = _device.GetDevice();
	if (_swapchain) {
		device.destroySwapchainKHR(_swapchain);
		_images.clear();
		_imageCount = 0;
	}
}

bool Swapchain::AcquireNextImage() {
	if (_suboptimal) {
		RecreateSwapchain();
		_suboptimal = false;
	}
	if (_acquiredImage != std::numeric_limits<uint32_t>::max()) { return true; }

	auto device = _device.GetDevice();

	constexpr static const int retryMax = 3;
	int retry                           = 0;

	_acquiredImage = std::numeric_limits<uint32_t>::max();
	while (retry < retryMax) {
		auto acquire = _device.RequestSemaphore();
		const auto acquireResult =
			device.acquireNextImageKHR(_swapchain, std::numeric_limits<uint64_t>::max(), acquire->GetSemaphore(), nullptr);

		if (acquireResult.result == vk::Result::eSuboptimalKHR) {
			_suboptimal = true;
			Log::Debug("[Vulkan::Swapchain] Swapchain is suboptimal, will recreate.");
		}

		if (acquireResult.result == vk::Result::eErrorOutOfDateKHR) {
			RecreateSwapchain();
			++retry;
			continue;
		}

		acquire->SignalExternal();
		_acquiredImage = acquireResult.value;
		_releaseSemaphores[_acquiredImage].Reset();
		_device.SetAcquireSemaphore({}, _acquiredImage, acquire);
		break;
	}

	return _acquiredImage != std::numeric_limits<uint32_t>::max();
}

void Swapchain::Present() {
	if (_acquiredImage == std::numeric_limits<uint32_t>::max()) { return; }

	auto device = _device.GetDevice();
	auto queues = _device.GetQueueInfo();
	auto queue  = queues.Queue(QueueType::Graphics);

	auto release          = _device.ConsumeReleaseSemaphore({});
	auto releaseSemaphore = release->GetSemaphore();
	const vk::PresentInfoKHR presentInfo(releaseSemaphore, _swapchain, _acquiredImage);
	const auto presentResult = queue.presentKHR(presentInfo);
	if (presentResult == vk::Result::eErrorOutOfDateKHR) {
		Log::Debug("[Vulkan::Swapchain] Failed to present out of date swapchain. Recreating.");
		RecreateSwapchain();
	} else {
		if (presentResult == vk::Result::eSuboptimalKHR) {
			Log::Debug("[Vulkan::Swapchain] Swapchain is suboptimal, will recreate.");
			_suboptimal = true;
		}
		release->WaitExternal();
		// We have to keep this semaphore handle alive until this swapchain image comes around again.
		_releaseSemaphores[_acquiredImage] = release;
	}

	_acquiredImage = std::numeric_limits<uint32_t>::max();
}

void Swapchain::RecreateSwapchain() {
	auto gpu     = _device.GetGPU();
	auto device  = _device.GetDevice();
	auto surface = _device.GetSurface();

	auto capabilities = gpu.getSurfaceCapabilitiesKHR(surface);

	if (capabilities.maxImageExtent.width == 0 && capabilities.maxImageExtent.height == 0) { return; }

	Log::Trace("[Vulkan::Swapchain] Recreating Swapchain.");

	auto windowSize = Window::Get()->GetSize();
	_extent         = vk::Extent2D(
    std::clamp(
      static_cast<uint32_t>(windowSize.x), capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
    std::clamp(
      static_cast<uint32_t>(windowSize.y), capabilities.minImageExtent.height, capabilities.maxImageExtent.height));
	_imageCount = std::clamp(3u, capabilities.minImageCount, capabilities.maxImageCount);

	const vk::SwapchainCreateInfoKHR swapchainCI({},
	                                             surface,
	                                             _imageCount,
	                                             _format.format,
	                                             _format.colorSpace,
	                                             _extent,
	                                             1,
	                                             vk::ImageUsageFlagBits::eColorAttachment,
	                                             vk::SharingMode::eExclusive,
	                                             {},
	                                             vk::SurfaceTransformFlagBitsKHR::eIdentity,
	                                             vk::CompositeAlphaFlagBitsKHR::eOpaque,
	                                             _presentMode,
	                                             VK_FALSE,
	                                             _swapchain);
	auto newSwapchain = device.createSwapchainKHR(swapchainCI);
	if (_swapchain) { device.destroySwapchainKHR(_swapchain); }
	_acquiredImage = std::numeric_limits<uint32_t>::max();
	_swapchain     = newSwapchain;
	_images        = device.getSwapchainImagesKHR(_swapchain);
	_imageCount    = static_cast<uint32_t>(_images.size());
	_releaseSemaphores.clear();
	_releaseSemaphores.resize(_imageCount);

	_device.SetupSwapchain({}, *this);
}
}  // namespace Vulkan
}  // namespace Luna
