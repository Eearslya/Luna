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

	// FIXME: Temporary patch to transition the swapchain image until we have render passes.
	{
		auto cmdBuf = cmd->GetCommandBuffer();
		auto image  = _swapchain->GetImage(swapchainImage);
		const vk::ImageMemoryBarrier barrier({},
		                                     {},
		                                     vk::ImageLayout::eUndefined,
		                                     vk::ImageLayout::ePresentSrcKHR,
		                                     VK_QUEUE_FAMILY_IGNORED,
		                                     VK_QUEUE_FAMILY_IGNORED,
		                                     image,
		                                     vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1));
		cmdBuf.pipelineBarrier(
			vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eBottomOfPipe, {}, {}, {}, barrier);
	}

	_device->Submit(cmd);

	_device->EndFrame();
	_swapchain->Present();
}
}  // namespace Luna
