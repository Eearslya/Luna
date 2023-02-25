#include <Luna/Utility/Log.hpp>
#include <Luna/Vulkan/Context.hpp>
#include <Luna/Vulkan/Device.hpp>
#include <Luna/Vulkan/Semaphore.hpp>
#include <Luna/Vulkan/WSI.hpp>

namespace Luna {
namespace Vulkan {
WSI::WSI(WSIPlatform* platform) : _platform(platform) {
	_context = MakeHandle<Context>(_platform->GetRequiredInstanceExtensions(), _platform->GetRequiredDeviceExtensions());
	_device  = MakeHandle<Device>(*_context);

	_surface = _platform->CreateSurface(_context->GetInstance());
	Log::Debug("Vulkan", "Surface created.");

	auto physicalDevice = _context->GetPhysicalDevice();

	uint32_t presentSupport = 0;
	for (const auto& family : _context->GetQueueInfo().Families) {
		if (family != VK_QUEUE_FAMILY_IGNORED) {
			if (physicalDevice.getSurfaceSupportKHR(family, _surface)) { presentSupport |= 1u << family; }
		}
	}
	if ((presentSupport & (1u << _context->GetQueueInfo().Family(QueueType::Graphics))) == 0) {
		throw std::runtime_error("Vulkan graphics queue does not support presentation!");
	}

	auto formats      = physicalDevice.getSurfaceFormatsKHR(_surface);
	auto presentModes = physicalDevice.getSurfacePresentModesKHR(_surface);

	_swapchainConfig.Format.format = vk::Format::eUndefined;
	for (const auto& format : formats) {
		if (format.format == vk::Format::eB8G8R8A8Srgb && format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear) {
			_swapchainConfig.Format = format;
			break;
		}
	}
	if (_swapchainConfig.Format.format == vk::Format::eUndefined) { _swapchainConfig.Format = formats[0]; }

	_swapchainConfig.PresentMode = vk::PresentModeKHR::eFifo;
	for (const auto& presentMode : presentModes) {
		if (presentMode == vk::PresentModeKHR::eMailbox) {
			_swapchainConfig.PresentMode = presentMode;
			break;
		}
	}

	RecreateSwapchain();
}

WSI::~WSI() noexcept {
	if (_context) {
		_context->GetDevice().waitIdle();
		if (_swapchain) { _context->GetDevice().destroySwapchainKHR(_swapchain); }
		if (_surface) { _context->GetInstance().destroySurfaceKHR(_surface); }
	}
}

void WSI::BeginFrame() {
	_platform->Update();
	_device->NextFrame();

	if (!_swapchain || _swapchainSuboptimal) { RecreateSwapchain(); }
	if (_swapchainAcquired != std::numeric_limits<uint32_t>::max()) { return; }

	auto device = _device->GetDevice();

	int retry = 0;
	while (retry < 3) {
		auto acquire = _device->RequestSemaphore();
		try {
			const auto acquireResult =
				device.acquireNextImageKHR(_swapchain, std::numeric_limits<uint64_t>::max(), acquire->GetSemaphore(), nullptr);

			if (acquireResult.result == vk::Result::eSuboptimalKHR) { _swapchainSuboptimal = true; }

			acquire->SignalExternal();
			_swapchainAcquired = acquireResult.value;
			_swapchainRelease[_swapchainAcquired].Reset();
			_device->SetAcquireSemaphore(_swapchainAcquired, acquire);

			break;
		} catch (const vk::OutOfDateKHRError& e) {
			RecreateSwapchain();
			++retry;

			continue;
		}
	}
}

void WSI::EndFrame() {
	if (_swapchainAcquired == std::numeric_limits<uint32_t>::max()) { return; }

	auto device = _context->GetDevice();
	auto queues = _context->GetQueueInfo();
	auto queue  = queues.Queue(QueueType::Graphics);

	_device->EndFrame();
	if (!_device->_swapchainAcquireConsumed) { return; }

	auto release          = _device->ConsumeReleaseSemaphore();
	auto releaseSemaphore = release->GetSemaphore();
	const vk::PresentInfoKHR presentInfo(releaseSemaphore, _swapchain, _swapchainAcquired);
	vk::Result presentResult = vk::Result::eSuccess;
	try {
		presentResult = queue.presentKHR(presentInfo);
		if (presentResult == vk::Result::eSuboptimalKHR) { _swapchainSuboptimal = true; }
		release->WaitExternal();
		_swapchainRelease[_swapchainAcquired] = release;
	} catch (const vk::OutOfDateKHRError& e) { RecreateSwapchain(); }

	_swapchainAcquired = std::numeric_limits<uint32_t>::max();
}

bool WSI::IsAlive() {
	return _platform && _platform->IsAlive();
}

void WSI::Update() {
	if (_platform) { _platform->Update(); }
}

void WSI::RecreateSwapchain() {
	auto physicalDevice = _context->GetPhysicalDevice();
	auto device         = _context->GetDevice();
	auto capabilities   = physicalDevice.getSurfaceCapabilitiesKHR(_surface);
	if (capabilities.maxImageExtent.width == 0 || capabilities.maxImageExtent.height == 0) { return; }

	const auto fbSize = _platform->GetFramebufferSize();
	_swapchainConfig.Extent =
		vk::Extent2D(glm::clamp(fbSize.x, capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
	               glm::clamp(fbSize.y, capabilities.minImageExtent.height, capabilities.maxImageExtent.height));

	uint32_t imageCount = glm::max(3u, capabilities.minImageCount);
	if (capabilities.maxImageCount > 0) { imageCount = glm::min(imageCount, capabilities.maxImageCount); }

	_swapchainConfig.Transform = vk::SurfaceTransformFlagBitsKHR::eIdentity;

	const vk::SwapchainCreateInfoKHR swapchainCI({},
	                                             _surface,
	                                             imageCount,
	                                             _swapchainConfig.Format.format,
	                                             _swapchainConfig.Format.colorSpace,
	                                             _swapchainConfig.Extent,
	                                             1,
	                                             vk::ImageUsageFlagBits::eColorAttachment,
	                                             vk::SharingMode::eExclusive,
	                                             nullptr,
	                                             _swapchainConfig.Transform,
	                                             vk::CompositeAlphaFlagBitsKHR::eOpaque,
	                                             _swapchainConfig.PresentMode,
	                                             VK_TRUE,
	                                             _swapchain);
	auto newSwapchain = device.createSwapchainKHR(swapchainCI);
	Log::Debug("Vulkan", "Swapchain created.");

	if (_swapchain) { device.destroySwapchainKHR(_swapchain); }
	_swapchain = newSwapchain;

	_swapchainAcquired = std::numeric_limits<uint32_t>::max();
	_swapchainImages   = device.getSwapchainImagesKHR(_swapchain);
	_swapchainRelease.clear();
	_swapchainRelease.resize(_swapchainImages.size());
	_swapchainSuboptimal = false;

	_device->SetupSwapchain(*this);
	OnSwapchainChanged(_swapchainConfig);
}
}  // namespace Vulkan
}  // namespace Luna
