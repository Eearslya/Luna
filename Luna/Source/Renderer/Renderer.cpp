#include <Luna/Core/Engine.hpp>
#include <Luna/Core/Window.hpp>
#include <Luna/Core/WindowManager.hpp>
#include <Luna/Renderer/RenderGraph.hpp>
#include <Luna/Renderer/RenderPass.hpp>
#include <Luna/Renderer/Renderer.hpp>
#include <Luna/Renderer/Swapchain.hpp>
#include <Luna/UI/UIManager.hpp>
#include <Luna/Utility/Color.hpp>
#include <Luna/Utility/Threading.hpp>
#include <Luna/Vulkan/Buffer.hpp>
#include <Luna/Vulkan/CommandBuffer.hpp>
#include <Luna/Vulkan/Context.hpp>
#include <Luna/Vulkan/Device.hpp>
#include <Luna/Vulkan/RenderPass.hpp>
#include <Tracy/Tracy.hpp>

namespace Luna {
struct SceneView {
	uint32_t Width  = 0;
	uint32_t Height = 0;
};

struct RenderGraphState {
	uint32_t Width  = 0;
	uint32_t Height = 0;
	std::vector<SceneView> SceneViews;
};
}  // namespace Luna

template <>
struct std::hash<Luna::RenderGraphState> {
	size_t operator()(const Luna::RenderGraphState& state) const {
		Luna::Hasher h;
		h(state.Width);
		h(state.Height);
		h(state.SceneViews.size());
		for (const auto& view : state.SceneViews) {
			h(view.Width);
			h(view.Height);
		}

		return static_cast<size_t>(h.Get());
	}
};

namespace Luna {
static struct RendererState {
	Vulkan::ContextHandle Context;
	Vulkan::DeviceHandle Device;
	RenderGraphState GraphState;
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

static void AddSceneView(int viewIndex) {
	const auto& view          = State.GraphState.SceneViews[viewIndex];
	const std::string attName = "SceneView-" + std::to_string(viewIndex);
	const auto Prefix         = [&attName](const std::string& str) { return attName + "/" + str; };

	const AttachmentInfo baseAttachment = {
		.SizeClass = SizeClass::Absolute, .Width = float(view.Width), .Height = float(view.Height)};

	AttachmentInfo color = baseAttachment;
	auto& pass           = State.Graph.AddPass(attName, RenderGraphQueueFlagBits::Graphics);

	pass.AddColorOutput(attName, color);

	pass.SetGetClearColor([viewIndex](uint32_t, vk::ClearColorValue* value) -> bool {
		if (value) {
			const auto col = HSVtoRGB({float(viewIndex) * 0.23f, 0.9f, 0.7f});
			*value         = vk::ClearColorValue(col.r, col.g, col.b, 1.0f);
		}
		return true;
	});
}

static void BakeRenderGraph() {
	auto physicalBuffers = State.Graph.ConsumePhysicalBuffers();
	State.Graph.Reset();
	State.Device->NextFrame();

	// Update swapchain dimensions and format.
	const auto& swapchainExtent = Engine::GetMainWindow()->GetSwapchain().GetExtent();
	const auto swapchainFormat  = Engine::GetMainWindow()->GetSwapchain().GetFormat();
	const Luna::ResourceDimensions backbufferDims{
		.Format = swapchainFormat, .Width = swapchainExtent.width, .Height = swapchainExtent.height};
	State.Graph.SetBackbufferDimensions(backbufferDims);

	// Scene Views
	for (int i = 0; i < State.GraphState.SceneViews.size(); ++i) { AddSceneView(i); }

	// UI Pass
	{
		Luna::AttachmentInfo uiColor;

		auto& ui = State.Graph.AddPass("UI", RenderGraphQueueFlagBits::Graphics);

		ui.AddColorOutput("UI", uiColor);
		for (int i = 0; i < State.GraphState.SceneViews.size(); ++i) {
			ui.AddTextureInput("SceneView-" + std::to_string(i), vk::PipelineStageFlagBits2::eFragmentShader);
		}

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

const Vulkan::ImageView& Renderer::GetSceneView(int view) {
	const std::string attachmentName = "SceneView-" + std::to_string(view);
	const auto& res                  = State.Graph.GetTextureResource(attachmentName);
	const auto& phys                 = State.Graph.GetPhysicalTextureResource(res);

	return phys;
}

int Renderer::RegisterSceneView(int width, int height) {
	if (width <= 0 || height <= 0) { return -1; }

	State.GraphState.SceneViews.push_back(SceneView{uint32_t(width), uint32_t(height)});

	return State.GraphState.SceneViews.size() - 1;
}

void Renderer::Render(double deltaTime) {
	ZoneScopedN("Renderer::Render");

	auto& device = *State.Device;
	device.NextFrame();

	if (Engine::GetMainWindow() && Engine::GetMainWindow()->GetSwapchain().Acquire()) {
		const auto windowSize   = Engine::GetMainWindow()->GetFramebufferSize();
		State.GraphState.Width  = uint32_t(windowSize.x);
		State.GraphState.Height = uint32_t(windowSize.y);
		const auto stateHash    = Hasher(State.GraphState).Get();
		if (stateHash != State.GraphHash) {
			BakeRenderGraph();
			State.GraphHash = stateHash;
		}

		TaskComposer composer;
		State.Graph.SetupAttachments(&State.Device->GetSwapchainView());
		State.Graph.EnqueueRenderPasses(*State.Device, composer);
		composer.GetOutgoingTask()->Wait();

		Engine::GetMainWindow()->GetSwapchain().Present();
	}

	State.GraphState.SceneViews.clear();
}
}  // namespace Luna
