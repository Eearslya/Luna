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
#include <Luna/Vulkan/Shader.hpp>
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

	auto vertData = filesystem->ReadBytes("Shaders/Basic.vert.spv");
	auto fragData = filesystem->ReadBytes("Shaders/Basic.frag.spv");
	if (vertData.has_value() && fragData.has_value()) {
		_program = _device->RequestProgram(vertData->size(), vertData->data(), fragData->size(), fragData->data());
	}
}

Graphics::~Graphics() noexcept {}

void Graphics::Update() {
	if (!BeginFrame()) { return; }

	auto cmd = _device->RequestCommandBuffer();

	const auto clearColor = std::pow(0.1f, 2.2f);

	auto rpInfo = _device->GetStockRenderPass(Vulkan::StockRenderPass::ColorOnly);
	rpInfo.ClearColors[0].setFloat32({clearColor, clearColor, clearColor, 1.0f});
	cmd->BeginRenderPass(rpInfo);
	cmd->SetProgram(_program);
	cmd->Draw(3);
	cmd->EndRenderPass();

	_device->Submit(cmd);

	EndFrame();
}

bool Graphics::BeginFrame() {
	_device->NextFrame();

	return _swapchain->AcquireNextImage();
}

void Graphics::EndFrame() {
	_device->EndFrame();
	_swapchain->Present();
}
}  // namespace Luna
