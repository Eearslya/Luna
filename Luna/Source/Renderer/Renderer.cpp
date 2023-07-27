#include <Luna/Core/WindowManager.hpp>
#include <Luna/Renderer/Renderer.hpp>
#include <Luna/Vulkan/Context.hpp>

namespace Luna {
static struct RendererState { Vulkan::ContextHandle Context; } State;

bool Renderer::Initialize() {
	const auto instanceExtensions                   = WindowManager::GetRequiredInstanceExtensions();
	const std::vector<const char*> deviceExtensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

	State.Context = MakeHandle<Vulkan::Context>(instanceExtensions, deviceExtensions);

	return true;
}

void Renderer::Shutdown() {
	State.Context.Reset();
}
}  // namespace Luna
