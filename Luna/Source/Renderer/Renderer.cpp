#include <Luna/Core/Engine.hpp>
#include <Luna/Core/Window.hpp>
#include <Luna/Core/WindowManager.hpp>
#include <Luna/Renderer/RenderContext.hpp>
#include <Luna/Renderer/RenderGraph.hpp>
#include <Luna/Renderer/RenderPass.hpp>
#include <Luna/Renderer/Renderer.hpp>
#include <Luna/Renderer/SceneRenderer.hpp>
#include <Luna/Renderer/Swapchain.hpp>
#include <Luna/Scene/Camera.hpp>
#include <Luna/Scene/EditorCamera.hpp>
#include <Luna/UI/UIManager.hpp>
#include <Luna/Utility/Color.hpp>
#include <Luna/Utility/Threading.hpp>
#include <Luna/Vulkan/Buffer.hpp>
#include <Luna/Vulkan/CommandBuffer.hpp>
#include <Luna/Vulkan/Context.hpp>
#include <Luna/Vulkan/Device.hpp>
#include <Luna/Vulkan/RenderPass.hpp>
#include <Tracy/Tracy.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace Luna {
struct SceneView {
	bool Active     = false;
	uint32_t Width  = 0;
	uint32_t Height = 0;

	RenderContext Context;
};

struct RenderGraphState {
	uint32_t Width  = 0;
	uint32_t Height = 0;
	std::array<SceneView, 8> SceneViews;
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
			h(view.Active);
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

static bool IsViewValid(int viewIndex) {
	const auto& view = State.GraphState.SceneViews[viewIndex];

	return view.Active && view.Width > 0 && view.Height > 0;
}

static void AddSceneView(int viewIndex) {
	if (!IsViewValid(viewIndex)) { return; }

	const auto& view          = State.GraphState.SceneViews[viewIndex];
	const std::string attName = "SceneView-" + std::to_string(viewIndex);
	const auto Prefix         = [&attName](const std::string& str) { return attName + "/" + str; };

	const AttachmentInfo baseAttachment = {
		.SizeClass = SizeClass::Absolute, .Width = float(view.Width), .Height = float(view.Height)};

	AttachmentInfo color = baseAttachment;
	auto& pass           = State.Graph.AddPass(attName, RenderGraphQueueFlagBits::Graphics);

	pass.AddColorOutput(attName, color);

	auto renderer = MakeHandle<SceneRenderer>(view.Context);
	pass.SetRenderPassInterface(renderer);
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
	for (int i = 0; i < State.GraphState.SceneViews.size(); ++i) {
		if (IsViewValid(i)) { AddSceneView(i); }
	}

	// UI Pass
	{
		Luna::AttachmentInfo uiColor;

		auto& ui = State.Graph.AddPass("UI", RenderGraphQueueFlagBits::Graphics);

		ui.AddColorOutput("UI", uiColor);
		for (int i = 0; i < State.GraphState.SceneViews.size(); ++i) {
			if (IsViewValid(i)) {
				ui.AddTextureInput("SceneView-" + std::to_string(i), vk::PipelineStageFlagBits2::eFragmentShader);
			}
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

int Renderer::RegisterSceneView() {
	int viewIndex = -1;
	for (int i = 0; i < State.GraphState.SceneViews.size(); ++i) {
		if (!State.GraphState.SceneViews[i].Active) {
			viewIndex = i;
			break;
		}
	}
	if (viewIndex == -1) { return -1; }

	auto& view  = State.GraphState.SceneViews[viewIndex];
	view.Active = true;
	view.Width  = 0;
	view.Height = 0;

	return viewIndex;
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
}

void Renderer::UnregisterSceneView(int viewIndex) {
	if (viewIndex < 0 || viewIndex >= State.GraphState.SceneViews.size()) { return; }

	State.GraphState.SceneViews[viewIndex].Active = false;
}

void Renderer::UpdateSceneView(int viewIndex, int width, int height, const EditorCamera& camera) {
	if (viewIndex < 0 || viewIndex >= State.GraphState.SceneViews.size()) { return; }

	auto& view  = State.GraphState.SceneViews[viewIndex];
	view.Width  = width;
	view.Height = height;
	view.Context.SetCamera(camera.GetProjection(), camera.GetView());
}
}  // namespace Luna
