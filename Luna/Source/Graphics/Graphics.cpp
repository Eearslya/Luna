#include <Luna/Graphics/Graphics.hpp>
#include <Luna/Vulkan/CommandBuffer.hpp>
#include <Luna/Vulkan/Context.hpp>
#include <Luna/Vulkan/Device.hpp>
#include <Luna/Vulkan/Fence.hpp>
#include <Luna/Vulkan/Swapchain.hpp>

namespace Luna {
Graphics::Graphics() {
	const auto instanceExtensions                   = Window::Get()->GetRequiredInstanceExtensions();
	const std::vector<const char*> deviceExtensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
	_context   = std::make_unique<Vulkan::Context>(instanceExtensions, deviceExtensions);
	_device    = std::make_unique<Vulkan::Device>(*_context);
	_swapchain = std::make_unique<Vulkan::Swapchain>(*_device);
}

Graphics::~Graphics() noexcept {}

void Graphics::Update() {
	uint32_t swapchainImage = _swapchain->AcquireNextImage();

	_device->NextFrame();

	auto cmd = _device->RequestCommandBuffer();

	_device->Submit(cmd);

	_device->EndFrame();
	_swapchain->Present();
}
}  // namespace Luna
