#include <imgui.h>

#include <Luna/Luna.hpp>
#include <Luna/Renderer/GlslCompiler.hpp>
#include <Luna/Renderer/RenderContext.hpp>
#include <Luna/Renderer/RenderGraph.hpp>
#include <Luna/Renderer/RenderPass.hpp>
#include <Luna/Scene/MeshRendererComponent.hpp>
#include <Luna/Vulkan/ImGuiRenderer.hpp>
#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <utility>

#include "ForwardRenderer.hpp"
#include "GBufferRenderer.hpp"
#include "IconsFontAwesome6.h"
#include "SceneLoader.hpp"

static bool LoadGraphicsShader(Luna::Vulkan::Device& device,
                               const Luna::Path& vertex,
                               const Luna::Path& fragment,
                               Luna::Vulkan::Program*& program) {
	Luna::GlslCompiler vertexCompiler, fragmentCompiler;

	vertexCompiler.SetSourceFromFile(vertex, Luna::Vulkan::ShaderStage::Vertex);
	fragmentCompiler.SetSourceFromFile(fragment, Luna::Vulkan::ShaderStage::Fragment);

	if (!vertexCompiler.Preprocess()) { return false; }
	if (!fragmentCompiler.Preprocess()) { return false; }

	std::vector<uint32_t> vertexSpv, fragmentSpv;
	std::string vertexError, fragmentError;
	vertexSpv = vertexCompiler.Compile(vertexError);
	if (vertexSpv.empty()) {
		Luna::Log::Error("Viewer", "Failed to compile Vertex shader: {}", vertexError);
		return false;
	}
	fragmentSpv = fragmentCompiler.Compile(fragmentError);
	if (fragmentSpv.empty()) {
		Luna::Log::Error("Viewer", "Failed to compile Fragment shader: {}", fragmentError);
		return false;
	}

	auto* newProgram = device.RequestProgram(
		vertexSpv.size() * sizeof(uint32_t), vertexSpv.data(), fragmentSpv.size() * sizeof(uint32_t), fragmentSpv.data());
	if (newProgram) {
		program = newProgram;
		return true;
	}
	return false;
};

class ViewerApplication : public Luna::Application {
 public:
	virtual void OnStart() override {
		auto* filesystem = Luna::Filesystem::Get();
		auto& device     = GetDevice();

		StyleImGui();

		{
			constexpr uint32_t width    = 1;
			constexpr uint32_t height   = 1;
			constexpr size_t pixelCount = width * height;
			uint32_t pixels[pixelCount];
			Luna::Vulkan::ImageInitialData initialImages[6];
			for (int i = 0; i < 6; ++i) { initialImages[i] = Luna::Vulkan::ImageInitialData{.Data = &pixels}; }
			const Luna::Vulkan::ImageCreateInfo imageCI2D = {
				.Domain        = Luna::Vulkan::ImageDomain::Physical,
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
			};

			std::fill(pixels, pixels + pixelCount, 0xff000000);
			_renderContext.GetDefaultImages().Black2D = device.CreateImage(imageCI2D, initialImages);

			std::fill(pixels, pixels + pixelCount, 0xff808080);
			_renderContext.GetDefaultImages().Gray2D = device.CreateImage(imageCI2D, initialImages);

			std::fill(pixels, pixels + pixelCount, 0xff800000);
			_renderContext.GetDefaultImages().Normal2D = device.CreateImage(imageCI2D, initialImages);

			std::fill(pixels, pixels + pixelCount, 0xffffffff);
			_renderContext.GetDefaultImages().White2D = device.CreateImage(imageCI2D, initialImages);
		}

		SceneLoader::LoadGltf(device, _scene, "assets://Models/Sponza/Sponza.gltf");

		Luna::Input::OnKey += [this](Luna::Key key, Luna::InputAction action, Luna::InputMods mods) {
			if (action == Luna::InputAction::Press && key == Luna::Key::F5) { LoadShaders(); }
		};
		LoadShaders();

		_swapchainConfig = GetSwapchainConfig();
		OnSwapchainChanged += [&](const Luna::Vulkan::SwapchainConfiguration& config) {
			_swapchainConfig = config;
			_swapchainDirty  = true;
		};

		_renderGraph = std::make_unique<Luna::RenderGraph>(device);
	}

	virtual void OnUpdate() override {
		if (_swapchainDirty) {
			BakeRenderGraph();
			_swapchainDirty = false;
		}

		Luna::TaskComposer composer;
		_renderGraph->SetupAttachments(&GetDevice().GetSwapchainView());
		UpdateScene(composer);
		RenderScene(composer);
		auto final = composer.GetOutgoingTask();
		final->Wait();
	}

	virtual void OnImGuiRender() override {}

 private:
	void BakeRenderGraph() {
		auto physicalBuffers = _renderGraph->ConsumePhysicalBuffers();

		_renderGraph->Reset();
		GetDevice().NextFrame();  // Release old Render Graph resources.

		// Update swapchain dimensions and format.
		const Luna::ResourceDimensions backbufferDims{.Format = _swapchainConfig.Format.format,
		                                              .Width  = _swapchainConfig.Extent.width,
		                                              .Height = _swapchainConfig.Extent.height};
		_renderGraph->SetBackbufferDimensions(backbufferDims);

		// Add GBuffer Render Pass.
		{
			Luna::AttachmentInfo albedo, normal, depth;
			albedo.Format = vk::Format::eR8G8B8A8Srgb;
			normal.Format = vk::Format::eR16G16Snorm;
			depth.Format  = GetDevice().GetDefaultDepthFormat();

			auto& gBuffer = _renderGraph->AddPass("GBuffer", Luna::RenderGraphQueueFlagBits::Graphics);

			gBuffer.AddColorOutput("GBuffer-Albedo", albedo);
			gBuffer.AddColorOutput("GBuffer-Normal", normal);
			gBuffer.SetDepthStencilOutput("Depth", depth);

			auto renderer = Luna::MakeHandle<GBufferRenderer>(_renderContext, _scene);
			gBuffer.SetRenderPassInterface(renderer);
		}

		// Add Lighting Render Pass.
		{
			Luna::AttachmentInfo lit;

			auto& lighting = _renderGraph->AddPass("Lighting", Luna::RenderGraphQueueFlagBits::Graphics);

			lighting.AddAttachmentInput("GBuffer-Albedo");
			lighting.AddAttachmentInput("GBuffer-Normal");
			lighting.SetDepthStencilInput("Depth");
			lighting.AddColorOutput("Lighting", lit);

			lighting.SetBuildRenderPass([&](Luna::Vulkan::CommandBuffer& cmd) {
				cmd.SetDepthWrite(false);
				cmd.SetInputAttachments(0, 0);
				cmd.SetProgram(_renderContext.GetShaders().PBRDeferred);
				cmd.Draw(3);
			});
		}

		_renderGraph->SetBackbufferSource("Lighting");

		_renderGraph->Bake();
		_renderGraph->InstallPhysicalBuffers(physicalBuffers);

		// _renderGraph->Log();
	}

	void StyleImGui() {
		ImGuiIO& io = ImGui::GetIO();

		io.ConfigWindowsMoveFromTitleBarOnly = true;

		// Style
		{
			auto& style = ImGui::GetStyle();

			// Main
			style.WindowPadding = ImVec2(8.0f, 8.0f);
			style.FramePadding  = ImVec2(5.0f, 3.0f);
			style.CellPadding   = ImVec2(4.0f, 2.0f);

			// Rounding
			style.WindowRounding    = 8.0f;
			style.ChildRounding     = 8.0f;
			style.FrameRounding     = 8.0f;
			style.PopupRounding     = 2.0f;
			style.ScrollbarRounding = 12.0f;
			style.GrabRounding      = 0.0f;
			style.LogSliderDeadzone = 4.0f;
			style.TabRounding       = 4.0f;
		}

		// Fonts
		{
			io.Fonts->Clear();

			io.Fonts->AddFontFromFileTTF("Resources/Fonts/Roboto-SemiMedium.ttf", 16.0f);

			ImFontConfig jpConfig;
			jpConfig.MergeMode = true;
			io.Fonts->AddFontFromFileTTF(
				"Resources/Fonts/NotoSansJP-Medium.otf", 18.0f, &jpConfig, io.Fonts->GetGlyphRangesJapanese());

			ImFontConfig faConfig;
			faConfig.MergeMode                 = true;
			faConfig.PixelSnapH                = true;
			static const ImWchar fontAwesome[] = {ICON_MIN_FA, ICON_MAX_16_FA, 0};
			io.Fonts->AddFontFromFileTTF("Resources/Fonts/FontAwesome6Free-Regular-400.otf", 16.0f, &faConfig, fontAwesome);
			io.Fonts->AddFontFromFileTTF("Resources/Fonts/FontAwesome6Free-Solid-900.otf", 16.0f, &faConfig, fontAwesome);
		}

		UpdateImGuiFontAtlas();
	}

	void UpdateScene(Luna::TaskComposer& composer) {
		auto& updates = composer.BeginPipelineStage();
		updates.Enqueue([this]() {
			const auto fbSize          = GetFramebufferSize();
			const float aspectRatio    = float(fbSize.x) / float(fbSize.y);
			const glm::mat4 projection = glm::perspective(glm::radians(60.0f), aspectRatio, 0.01f, 1000.0f);
			const glm::mat4 view       = glm::lookAt(glm::vec3(2, 1.0f, 1), glm::vec3(0, 0.8f, 0), glm::vec3(0, 1, 0));
			_renderContext.SetCamera(projection, view);
		});
	}

	void RenderScene(Luna::TaskComposer& composer) {
		_renderGraph->EnqueueRenderPasses(GetDevice(), composer);
	}

	void LoadShaders() {
		auto& shaders = _renderContext.GetShaders();

		if (!LoadGraphicsShader(GetDevice(),
		                        "res://Shaders/PBRForward.vert.glsl",
		                        "res://Shaders/PBRForward.frag.glsl",
		                        shaders.PBRForward)) {
			return;
		}
		if (!LoadGraphicsShader(GetDevice(),
		                        "res://Shaders/PBRGBuffer.vert.glsl",
		                        "res://Shaders/PBRGBuffer.frag.glsl",
		                        shaders.PBRGBuffer)) {
			return;
		}
		if (!LoadGraphicsShader(GetDevice(),
		                        "res://Shaders/PBRDeferred.vert.glsl",
		                        "res://Shaders/PBRDeferred.frag.glsl",
		                        shaders.PBRDeferred)) {
			return;
		}

		Luna::Log::Info("Viewer", "Shaders reloaded.");
	}

	Luna::RenderContext _renderContext;
	std::unique_ptr<Luna::RenderGraph> _renderGraph;
	Luna::Vulkan::SwapchainConfiguration _swapchainConfig;
	bool _swapchainDirty = true;

	Luna::Scene _scene;
};

Luna::Application* Luna::CreateApplication(int argc, const char** argv) {
	return new ViewerApplication();
}
