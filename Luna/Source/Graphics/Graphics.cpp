#include <stb_image.h>

#include <Luna/Core/Log.hpp>
#include <Luna/Graphics/Graphics.hpp>
#include <Luna/Vulkan/Buffer.hpp>
#include <Luna/Vulkan/CommandBuffer.hpp>
#include <Luna/Vulkan/Context.hpp>
#include <Luna/Vulkan/Device.hpp>
#include <Luna/Vulkan/Fence.hpp>
#include <Luna/Vulkan/Image.hpp>
#include <Luna/Vulkan/RenderPass.hpp>
#include <Luna/Vulkan/Swapchain.hpp>

namespace Luna {
Graphics::Graphics() {
	auto filesystem = Filesystem::Get();

	filesystem->AddSearchPath("Assets");

	const auto instanceExtensions                   = Window::Get()->GetRequiredInstanceExtensions();
	const std::vector<const char*> deviceExtensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
	_context   = std::make_unique<Vulkan::Context>(instanceExtensions, deviceExtensions);
	_device    = std::make_unique<Vulkan::Device>(*_context);
	_swapchain = std::make_unique<Vulkan::Swapchain>(*_device);

	uint8_t data[128] = {0, 1, 2, 3, 4, 5, 6, 7, 8};
	auto buffer       = _device->CreateBuffer(
    Vulkan::BufferCreateInfo(Vulkan::BufferDomain::Device, 128, vk::BufferUsageFlagBits::eStorageBuffer), data);

	auto imageData = filesystem->ReadBytes("Images/Test.jpg");
	if (imageData.has_value()) {
		int w, h;
		stbi_uc* pixels = stbi_load_from_memory(
			reinterpret_cast<const stbi_uc*>(imageData->data()), imageData->size(), &w, &h, nullptr, STBI_rgb_alpha);
		if (pixels) {
			const Vulkan::InitialImageData initialImage{.Data = pixels};
			auto image = _device->CreateImage(
				Vulkan::ImageCreateInfo::Immutable2D(vk::Format::eR8G8B8A8Unorm, vk::Extent2D(w, h), true), &initialImage);
		} else {
			Log::Error("[Graphics] Failed to load test texture: {}", stbi_failure_reason());
		}
	}
}

Graphics::~Graphics() noexcept {}

void Graphics::Update() {
	uint32_t swapchainImage = _swapchain->AcquireNextImage();

	_device->NextFrame();

	auto cmd = _device->RequestCommandBuffer();

	auto rpInfo = _device->GetStockRenderPass(Vulkan::StockRenderPass::ColorOnly);
	auto& pass  = _device->RequestRenderPass(rpInfo);

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
