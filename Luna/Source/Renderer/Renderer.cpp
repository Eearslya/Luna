#include <Luna/Core/Engine.hpp>
#include <Luna/Core/Window.hpp>
#include <Luna/Core/WindowManager.hpp>
#include <Luna/Renderer/Renderer.hpp>
#include <Luna/Renderer/Swapchain.hpp>
#include <Luna/Vulkan/Buffer.hpp>
#include <Luna/Vulkan/CommandBuffer.hpp>
#include <Luna/Vulkan/Context.hpp>
#include <Luna/Vulkan/Device.hpp>
#include <Luna/Vulkan/RenderPass.hpp>

namespace Luna {
static struct RendererState {
	Vulkan::ContextHandle Context;
	Vulkan::DeviceHandle Device;
} State;

bool Renderer::Initialize() {
	const auto instanceExtensions                   = WindowManager::GetRequiredInstanceExtensions();
	const std::vector<const char*> deviceExtensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

	State.Context = MakeHandle<Vulkan::Context>(instanceExtensions, deviceExtensions);
	State.Device  = MakeHandle<Vulkan::Device>(*State.Context);

	return true;
}

void Renderer::Shutdown() {
	State.Device.Reset();
	State.Context.Reset();
}

Vulkan::Device& Renderer::GetDevice() {
	return *State.Device;
}

void Renderer::Render() {
	auto& device = *State.Device;
	device.NextFrame();

	if (!Engine::GetMainWindow()) { return; }

	const bool acquired = Engine::GetMainWindow()->GetSwapchain().Acquire();
	if (acquired) {
		auto cmd = device.RequestCommandBuffer();

		auto rpInfo           = device.GetSwapchainRenderPass();
		rpInfo.ClearColors[0] = vk::ClearColorValue(0.36f, 0.0f, 0.63f, 1.0f);
		cmd->BeginRenderPass(rpInfo);
		cmd->EndRenderPass();

		device.Submit(cmd);

		Engine::GetMainWindow()->GetSwapchain().Present();
	}
}
}  // namespace Luna
