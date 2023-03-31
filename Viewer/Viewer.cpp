#include <imgui.h>

#include <Luna/Luna.hpp>
#include <Luna/Renderer/Environment.hpp>
#include <Luna/Renderer/GlslCompiler.hpp>
#include <Luna/Renderer/RenderContext.hpp>
#include <Luna/Renderer/RenderGraph.hpp>
#include <Luna/Renderer/RenderPass.hpp>
#include <Luna/Scene/MeshRendererComponent.hpp>
#include <Luna/Scene/SkyLightComponent.hpp>
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

struct LightingData {
	alignas(16) glm::mat4 InverseViewProjection;
	alignas(16) glm::vec3 CameraPosition;
	float IBLStrength;
	glm::vec2 InverseResolution;
	float PrefilteredMipLevels;
	uint32_t Irradiance;
	uint32_t Prefiltered;
	uint32_t Brdf;
};

class ViewerApplication : public Luna::Application {
 public:
	virtual void OnStart() override {
		auto* filesystem = Luna::Filesystem::Get();
		auto& device     = GetDevice();

		StyleImGui();

		SceneLoader::LoadGltf(device, _scene, "assets://Models/DamagedHelmet/DamagedHelmet.gltf");
		SceneLoader::LoadGltf(device, _scene, "assets://Models/Sponza/Sponza.gltf");

		auto environment = Luna::MakeHandle<Luna::Environment>(device, "assets://Environments/TokyoBigSight.hdr");
		auto skyLight    = _scene.CreateEntity("Sky Light");
		skyLight.AddComponent<Luna::SkyLightComponent>(environment);

		_swapchainConfig = GetSwapchainConfig();
		OnSwapchainChanged += [&](const Luna::Vulkan::SwapchainConfiguration& config) {
			_swapchainConfig = config;
			_swapchainDirty  = true;
		};

		_renderContext = std::make_unique<Luna::RenderContext>(device);
		_renderGraph   = std::make_unique<Luna::RenderGraph>(device);

		Luna::Input::OnKey += [this](Luna::Key key, Luna::InputAction action, Luna::InputMods mods) {
			if (action == Luna::InputAction::Press && key == Luna::Key::F5) { _renderContext->ReloadShaders(); }
		};
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

		Luna::AttachmentInfo emissive;
		emissive.Format = vk::Format::eR16G16B16A16Sfloat;
		if (GetDevice().IsFormatSupported(
					vk::Format::eB10G11R11UfloatPack32, vk::FormatFeatureFlagBits::eColorAttachment, vk::ImageTiling::eOptimal)) {
			emissive.Format = vk::Format::eB10G11R11UfloatPack32;
		}

		// Add GBuffer Render Pass.
		{
			Luna::AttachmentInfo albedo, normal, pbr, depth;
			albedo.Format = vk::Format::eR8G8B8A8Srgb;
			normal.Format = vk::Format::eR16G16Snorm;
			pbr.Format    = vk::Format::eR8G8B8A8Unorm;
			depth.Format  = GetDevice().GetDefaultDepthFormat();

			auto& gBuffer = _renderGraph->AddPass("GBuffer", Luna::RenderGraphQueueFlagBits::Graphics);

			gBuffer.AddColorOutput("GBuffer-Albedo", albedo);
			gBuffer.AddColorOutput("GBuffer-Normal", normal);
			gBuffer.AddColorOutput("GBuffer-PBR", pbr);
			gBuffer.AddColorOutput("GBuffer-Emissive", emissive);
			gBuffer.SetDepthStencilOutput("Depth", depth);

			auto renderer = Luna::MakeHandle<GBufferRenderer>(*_renderContext, _scene);
			gBuffer.SetRenderPassInterface(renderer);
		}

		// Add Lighting Render Pass.
		{
			auto& lighting = _renderGraph->AddPass("Lighting", Luna::RenderGraphQueueFlagBits::Graphics);

			lighting.AddAttachmentInput("GBuffer-Albedo");
			lighting.AddAttachmentInput("GBuffer-Normal");
			lighting.AddAttachmentInput("GBuffer-PBR");
			lighting.AddAttachmentInput("Depth");
			lighting.AddColorOutput("Lighting", emissive, "GBuffer-Emissive");

			lighting.SetBuildRenderPass([&](Luna::Vulkan::CommandBuffer& cmd) {
				const auto& dims = _renderGraph->GetResourceDimensions(_renderGraph->GetTextureResource("Lighting"));

				LightingData light          = {};
				const auto renderParams     = _renderContext->GetRenderParameters();
				light.CameraPosition        = renderParams.CameraPosition;
				light.InverseViewProjection = renderParams.InvViewProjection;
				light.InverseResolution     = glm::vec2(1.0f / float(dims.Width), 1.0f / float(dims.Height));

				const auto& registry = _scene.GetRegistry();
				auto environments    = registry.view<Luna::SkyLightComponent>();
				if (environments.size() > 0) {
					const auto envId = environments[0];
					const Luna::Entity env(envId, _scene);
					const auto& skyLight = env.GetComponent<Luna::SkyLightComponent>();

					light.IBLStrength          = 1.0f;
					light.Irradiance           = _renderContext->SetTexture(skyLight.Environment->Irradiance->GetView(),
                                                        Luna::Vulkan::StockSampler::TrilinearClamp);
					light.Prefiltered          = _renderContext->SetTexture(skyLight.Environment->Prefiltered->GetView(),
                                                         Luna::Vulkan::StockSampler::TrilinearClamp);
					light.PrefilteredMipLevels = skyLight.Environment->Prefiltered->GetCreateInfo().MipLevels;
					light.Brdf                 = _renderContext->SetTexture(skyLight.Environment->BrdfLut->GetView(),
                                                  Luna::Vulkan::StockSampler::LinearClamp);
				}

				cmd.SetBlendEnable(true);
				cmd.SetColorBlend(vk::BlendFactor::eOne, vk::BlendOp::eAdd, vk::BlendFactor::eOne);
				cmd.SetDepthWrite(false);
				cmd.SetInputAttachments(0, 0);
				cmd.SetBindless(1, _renderContext->GetBindlessSet());
				cmd.SetProgram(_renderContext->GetShaders().PBRDeferred->GetProgram());
				cmd.PushConstants(&light, 0, sizeof(light));
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
			_renderContext->BeginFrame(GetDevice().GetFrameIndex());
			_renderContext->SetCamera(projection, view);
		});
	}

	void RenderScene(Luna::TaskComposer& composer) {
		_renderGraph->EnqueueRenderPasses(GetDevice(), composer);
	}

	std::unique_ptr<Luna::RenderContext> _renderContext;
	std::unique_ptr<Luna::RenderGraph> _renderGraph;
	Luna::Vulkan::SwapchainConfiguration _swapchainConfig;
	bool _swapchainDirty = true;

	Luna::Scene _scene;
};

Luna::Application* Luna::CreateApplication(int argc, const char** argv) {
	return new ViewerApplication();
}
