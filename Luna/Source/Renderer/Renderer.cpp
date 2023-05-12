#include <Luna/Core/Engine.hpp>
#include <Luna/Core/Window.hpp>
#include <Luna/Core/WindowManager.hpp>
#include <Luna/Renderer/RenderContext.hpp>
#include <Luna/Renderer/RenderGraph.hpp>
#include <Luna/Renderer/RenderPass.hpp>
#include <Luna/Renderer/RenderRunner.hpp>
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
#include <Luna/Vulkan/Image.hpp>
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
	std::array<RenderRunnerHandle, RendererSuiteTypeCount> Runners;
	DefaultImages DefaultImages;
} State;

bool Renderer::Initialize() {
	ZoneScopedN("Renderer::Initialize");

	const auto instanceExtensions                   = WindowManager::GetRequiredInstanceExtensions();
	const std::vector<const char*> deviceExtensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

	State.Context = MakeHandle<Vulkan::Context>(instanceExtensions, deviceExtensions);
	State.Device  = MakeHandle<Vulkan::Device>(*State.Context);

	State.Runners[int(RendererSuiteType::ForwardOpaque)]      = MakeHandle<RenderRunner>(RendererType::GeneralForward);
	State.Runners[int(RendererSuiteType::ForwardTransparent)] = MakeHandle<RenderRunner>(RendererType::GeneralForward);
	State.Runners[int(RendererSuiteType::PrepassDepth)]       = MakeHandle<RenderRunner>(RendererType::DepthOnly);
	State.Runners[int(RendererSuiteType::Deferred)]           = MakeHandle<RenderRunner>(RendererType::GeneralDeferred);

	// Create placeholder textures.
	{
		// All textures will be 4x4 to allow for minimum texel size.
		constexpr uint32_t width    = 4;
		constexpr uint32_t height   = 4;
		constexpr size_t pixelCount = width * height;
		uint32_t pixels[pixelCount];

		const Vulkan::ImageCreateInfo imageCI2D = {
			.Domain        = Vulkan::ImageDomain::Physical,
			.Width         = width,
			.Height        = height,
			.Depth         = 1,
			.MipLevels     = 1,
			.ArrayLayers   = 1,
			.Format        = vk::Format::eR8G8B8A8Unorm,
			.InitialLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
			.Type          = vk::ImageType::e2D,
			.Usage         = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eInputAttachment,
			.Samples       = vk::SampleCountFlagBits::e1,
			.MiscFlags     = Vulkan::ImageCreateFlagBits::MutableSrgb,
		};

		Vulkan::ImageInitialData initialImages[6];
		for (int i = 0; i < 6; ++i) { initialImages[i] = Vulkan::ImageInitialData{.Data = &pixels}; }

		// Black images
		std::fill(pixels, pixels + pixelCount, 0xff000000);
		State.DefaultImages.Black2D = State.Device->CreateImage(imageCI2D, initialImages);

		// Gray images
		std::fill(pixels, pixels + pixelCount, 0xff808080);
		State.DefaultImages.Gray2D = State.Device->CreateImage(imageCI2D, initialImages);

		// Normal images
		std::fill(pixels, pixels + pixelCount, 0xffff8080);
		State.DefaultImages.Normal2D = State.Device->CreateImage(imageCI2D, initialImages);

		// White images
		std::fill(pixels, pixels + pixelCount, 0xffffffff);
		State.DefaultImages.White2D = State.Device->CreateImage(imageCI2D, initialImages);
	}

	return true;
}

void Renderer::Shutdown() {
	ZoneScopedN("Renderer::Shutdown");

	State.DefaultImages.White2D.Reset();
	State.DefaultImages.Normal2D.Reset();
	State.DefaultImages.Gray2D.Reset();
	State.DefaultImages.Black2D.Reset();
	for (auto& runner : State.Runners) { runner.Reset(); }
	State.Graph.Reset();
	State.Device.Reset();
	State.Context.Reset();
}

DefaultImages& Renderer::GetDefaultImages() {
	return State.DefaultImages;
}

Vulkan::Device& Renderer::GetDevice() {
	return *State.Device;
}

RenderRunner& Renderer::GetRunner(RendererSuiteType type) {
	return *State.Runners[int(type)];
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
	AttachmentInfo depth = baseAttachment.Copy().SetFormat(Renderer::GetDevice().GetDefaultDepthFormat());
	auto& pass           = State.Graph.AddPass(attName, RenderGraphQueueFlagBits::Graphics);

	pass.SetDepthStencilOutput(Prefix("Depth"), depth);
	pass.AddColorOutput(attName, color);

	SceneRendererFlags flags = SceneRendererFlagBits::ForwardOpaque | SceneRendererFlagBits::ForwardTransparent;

	auto renderer = MakeHandle<SceneRenderer>(view.Context, flags);
	pass.SetRenderPassInterface(renderer);
}

static void BakeRenderGraph() {
	ZoneScopedN("Renderer::BakeRenderGraph");

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

	if (!Engine::GetMainWindow()) { return; }

	bool acquired = false;
	{
		ZoneScopedN("Swapchain Acquire");
		acquired = Engine::GetMainWindow()->GetSwapchain().Acquire();
	}

	if (acquired) {
		{
			ZoneScopedN("RenderGraph Update");

			const auto windowSize   = Engine::GetMainWindow()->GetFramebufferSize();
			State.GraphState.Width  = uint32_t(windowSize.x);
			State.GraphState.Height = uint32_t(windowSize.y);
			const auto stateHash    = Hasher(State.GraphState).Get();
			if (stateHash != State.GraphHash) {
				BakeRenderGraph();
				State.GraphHash = stateHash;
			}
		}

		TaskComposer composer;

		{
			ZoneScopedN("RenderGraph Enqueue");
			State.Graph.SetupAttachments(&State.Device->GetSwapchainView());
			State.Graph.EnqueueRenderPasses(*State.Device, composer);
		}

		{
			ZoneScopedN("RenderGraph Execute");
			composer.GetOutgoingTask()->Wait();
		}

		{
			ZoneScopedN("Swapchain Present");
			Engine::GetMainWindow()->GetSwapchain().Present();
		}
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
