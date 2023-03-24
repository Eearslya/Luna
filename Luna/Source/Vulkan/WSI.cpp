#include <Luna/Utility/Log.hpp>
#include <Luna/Vulkan/Context.hpp>
#include <Luna/Vulkan/Device.hpp>
#include <Luna/Vulkan/Semaphore.hpp>
#include <Luna/Vulkan/WSI.hpp>
#include <Tracy/Tracy.hpp>

namespace Luna {
namespace Vulkan {
WSI::WSI(WSIPlatform* platform) : _platform(platform) {
	_platform->Initialize();
	_context = MakeHandle<Context>(_platform->GetRequiredInstanceExtensions(), _platform->GetRequiredDeviceExtensions());
	_device  = MakeHandle<Device>(*_context);
	_surface = _platform->CreateSurface(_context->GetInstance());

	const auto& deviceInfo  = _context->GetDeviceInfo();
	const auto capabilities = deviceInfo.PhysicalDevice.getSurfaceCapabilitiesKHR(_surface);
	const auto formats      = deviceInfo.PhysicalDevice.getSurfaceFormatsKHR(_surface);
	const auto presentModes = deviceInfo.PhysicalDevice.getSurfacePresentModesKHR(_surface);

	_swapchainConfig.Format.format = vk::Format::eUndefined;
	for (const auto& format : formats) {
		if (format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear &&
		    (format.format == vk::Format::eR8G8B8A8Srgb || format.format == vk::Format::eB8G8R8A8Srgb ||
		     format.format == vk::Format::eA8B8G8R8SrgbPack32)) {
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

	_swapchainConfig.Transform = vk::SurfaceTransformFlagBitsKHR::eIdentity;
}

WSI::~WSI() noexcept {
	if (_context) {
		_context->GetDevice().waitIdle();
		if (_swapchain) { _context->GetDevice().destroySwapchainKHR(_swapchain); }
		if (_surface) { _context->GetInstance().destroySurfaceKHR(_surface); }
	}
	_platform->Shutdown();
}

InputAction WSI::GetButton(MouseButton button) const {
	return _platform->GetButton(button);
}

glm::uvec2 WSI::GetFramebufferSize() const {
	return _platform->GetFramebufferSize();
}

InputAction WSI::GetKey(Key key) const {
	return _platform->GetKey(key);
}

double WSI::GetTime() const {
	return _platform->GetTime();
}

glm::uvec2 WSI::GetWindowSize() const {
	return _platform->GetWindowSize();
}

void WSI::BeginFrame() {
	ZoneScopedN("WSI::BeginFrame");

	_platform->Update();
	_device->NextFrame();

	if (!_swapchain || _swapchainSuboptimal) { RecreateSwapchain(); }
	if (_swapchainAcquired != NotAcquired) { return; }

	auto device = _context->GetDevice();

	{
		ZoneScopedN("AcquireNextImage");

		int retry = 0;
		while (retry < 3) {
			auto acquire = _device->RequestSemaphore();

			try {
				const auto acquireResult = device.acquireNextImageKHR(
					_swapchain, std::numeric_limits<uint64_t>::max(), acquire->GetSemaphore(), nullptr);

				if (acquireResult.result == vk::Result::eSuboptimalKHR) { _swapchainSuboptimal = true; }

				acquire->SignalExternal();
				// acquire->SetForeignQueue();
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
}

void WSI::EndFrame() {
	ZoneScopedN("WSI::EndFrame");

	if (_swapchainAcquired == NotAcquired) { return; }

	auto device        = _context->GetDevice();
	const auto& queues = _context->GetQueueInfo();
	auto queue         = queues.Queue(QueueType::Graphics);

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

	_swapchainAcquired = NotAcquired;
}

bool WSI::IsAlive() {
	return _platform && _platform->IsAlive();
}

void WSI::Update() {
	if (_platform) { _platform->Update(); }
}

void WSI::RecreateSwapchain() {
	auto physicalDevice     = _context->GetDeviceInfo().PhysicalDevice;
	auto device             = _context->GetDevice();
	const auto capabilities = physicalDevice.getSurfaceCapabilitiesKHR(_surface);
	const auto fbSize       = _platform->GetFramebufferSize();

	if (fbSize.x == 0 || fbSize.y == 0 || capabilities.maxImageExtent.width == 0 ||
	    capabilities.maxImageExtent.height == 0) {
		return;
	}

	_swapchainConfig.Extent =
		vk::Extent2D(glm::clamp(fbSize.x, capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
	               glm::clamp(fbSize.y, capabilities.minImageExtent.height, capabilities.maxImageExtent.height));

	uint32_t imageCount = glm::max(3u, capabilities.minImageCount);
	if (capabilities.maxImageCount > 0) { imageCount = glm::min(imageCount, capabilities.maxImageCount); }

	const vk::SwapchainCreateInfoKHR swapchainCI(
		{},
		_surface,
		imageCount,
		_swapchainConfig.Format.format,
		_swapchainConfig.Format.colorSpace,
		_swapchainConfig.Extent,
		1,
		vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferDst,
		vk::SharingMode::eExclusive,
		nullptr,
		_swapchainConfig.Transform,
		vk::CompositeAlphaFlagBitsKHR::eOpaque,
		_swapchainConfig.PresentMode,
		VK_TRUE,
		_swapchain);
	auto newSwapchain = device.createSwapchainKHR(swapchainCI);
	Log::Trace("Vulkan", "Swapchain created.");

	if (_swapchain) { device.destroySwapchainKHR(_swapchain); }
	_swapchain = newSwapchain;

	_swapchainAcquired = NotAcquired;
	_swapchainImages   = device.getSwapchainImagesKHR(_swapchain);
	_swapchainRelease.clear();
	_swapchainRelease.resize(_swapchainImages.size());
	_swapchainSuboptimal = false;

	_device->SetupSwapchain(*this);

	OnSwapchainChanged(_swapchainConfig);
}
}  // namespace Vulkan
}  // namespace Luna
