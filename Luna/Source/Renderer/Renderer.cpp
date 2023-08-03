#include <Luna/Core/Engine.hpp>
#include <Luna/Core/Window.hpp>
#include <Luna/Core/WindowManager.hpp>
#include <Luna/Renderer/Renderer.hpp>
#include <Luna/Vulkan/CommandBuffer.hpp>
#include <Luna/Vulkan/Context.hpp>
#include <Luna/Vulkan/Device.hpp>

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

void Renderer::Render() {
	auto& device = *State.Device;
	device.NextFrame();

	if (!Engine::GetMainWindow()) { return; }

	auto cmd = device.RequestCommandBuffer();
	device.Submit(cmd);
}
}  // namespace Luna
