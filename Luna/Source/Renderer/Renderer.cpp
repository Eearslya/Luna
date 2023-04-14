#include <Luna/Core/Window.hpp>
#include <Luna/Core/WindowManager.hpp>
#include <Luna/Renderer/RenderGraph.hpp>
#include <Luna/Renderer/RenderPass.hpp>
#include <Luna/Renderer/Renderer.hpp>
#include <Luna/Renderer/Swapchain.hpp>
#include <Luna/UI/UIManager.hpp>
#include <Luna/Utility/Threading.hpp>
#include <Luna/Vulkan/Buffer.hpp>
#include <Luna/Vulkan/CommandBuffer.hpp>
#include <Luna/Vulkan/Context.hpp>
#include <Luna/Vulkan/Device.hpp>
#include <Luna/Vulkan/RenderPass.hpp>
#include <Tracy/Tracy.hpp>

namespace Luna {
struct RenderGraphState {
	uint32_t Width  = 0;
	uint32_t Height = 0;
};
}  // namespace Luna

template <>
struct std::hash<Luna::RenderGraphState> {
	size_t operator()(const Luna::RenderGraphState& state) const {
		Luna::Hasher h;
		h(state.Width);
		h(state.Height);

		return static_cast<size_t>(h.Get());
	}
};

namespace Luna {
static struct RendererState {
	Vulkan::ContextHandle Context;
	Vulkan::DeviceHandle Device;
	Window* MainWindow;
	const Scene* ActiveScene;
	Hash GraphHash = 0;
	RenderGraph Graph;
} State;

bool Renderer::Initialize() {
	ZoneScopedN("Renderer::Initialize");

	const auto instanceExtensions                   = WindowManager::GetRequiredInstanceExtensions();
	const std::vector<const char*> deviceExtensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

	State.Context = MakeHandle<Vulkan::Context>(instanceExtensions, deviceExtensions);
	State.Device  = MakeHandle<Vulkan::Device>(*State.Context);

	return true;
}

void Renderer::Shutdown() {
	ZoneScopedN("Renderer::Shutdown");

	State.Graph.Reset();
	State.Device.Reset();
	State.Context.Reset();
}

Vulkan::Device& Renderer::GetDevice() {
	return *State.Device;
}

static void BakeRenderGraph() {
	auto physicalBuffers = State.Graph.ConsumePhysicalBuffers();
	State.Graph.Reset();
	State.Device->NextFrame();

	// Update swapchain dimensions and format.
	const auto& swapchainExtent = State.MainWindow->GetSwapchain().GetExtent();
	const auto swapchainFormat  = State.MainWindow->GetSwapchain().GetFormat();
	const Luna::ResourceDimensions backbufferDims{
		.Format = swapchainFormat, .Width = swapchainExtent.width, .Height = swapchainExtent.height};
	State.Graph.SetBackbufferDimensions(backbufferDims);

	{
		Luna::AttachmentInfo uiColor;

		auto& ui = State.Graph.AddPass("UI", RenderGraphQueueFlagBits::Graphics);

		ui.AddColorOutput("UI", uiColor);

		ui.SetGetClearColor([](uint32_t, vk::ClearColorValue* value) -> bool {
			if (value) { *value = vk::ClearColorValue(0.0f, 0.0f, 0.0f, 1.0f); }
			return true;
		});
		ui.SetBuildRenderPass([](Vulkan::CommandBuffer& cmd) { UIManager::Render(cmd); });
	}

	State.Graph.SetBackbufferSource("UI");
	State.Graph.Bake();
	State.Graph.InstallPhysicalBuffers(physicalBuffers);
}

void Renderer::Render(double deltaTime) {
	ZoneScopedN("Renderer::Render");

	auto& device = *State.Device;
	device.NextFrame();

	if (State.MainWindow && State.MainWindow->GetSwapchain().Acquire()) {
		const auto windowSize = State.MainWindow->GetFramebufferSize();
		const RenderGraphState state{.Width = uint32_t(windowSize.x), .Height = uint32_t(windowSize.y)};
		const auto stateHash = Hasher(state).Get();
		if (stateHash != State.GraphHash) {
			BakeRenderGraph();
			State.GraphHash = stateHash;
		}

		TaskComposer composer;
		State.Graph.SetupAttachments(&State.Device->GetSwapchainView());
		State.Graph.EnqueueRenderPasses(*State.Device, composer);
		composer.GetOutgoingTask()->Wait();

		State.MainWindow->GetSwapchain().Present();
	}
}

void Renderer::SetMainWindow(Window& window) {
	State.MainWindow = &window;
}

void Renderer::SetScene(const Scene& scene) {
	State.ActiveScene = &scene;
}
}  // namespace Luna
