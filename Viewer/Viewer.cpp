#include <imgui.h>

#include <Luna/Application/Input.hpp>
#include <Luna/Luna.hpp>
#include <Luna/Renderer/RenderContext.hpp>
#include <Luna/Renderer/RenderGraph.hpp>
#include <Luna/Renderer/RenderPass.hpp>
#include <Luna/Renderer/RendererSuite.hpp>
#include <Luna/Scene/MeshRendererComponent.hpp>
#include <Luna/Scene/Scene.hpp>
#include <Luna/Utility/Log.hpp>
#include <Luna/Vulkan/ImGuiRenderer.hpp>
#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <utility>

#include "IconsFontAwesome6.h"
#include "SceneLoader.hpp"
#include "SceneRenderer.hpp"

class ViewerApplication : public Luna::Application {
 public:
	virtual void OnStart() override {
		auto& device = GetDevice();

		StyleImGui();

		_swapchainConfig = GetSwapchainConfig();
		OnSwapchainChanged += [&](const Luna::Vulkan::SwapchainConfiguration& config) {
			_swapchainConfig = config;
			_swapchainDirty  = true;
		};

		_renderGraph = std::make_unique<Luna::RenderGraph>(device);
		_renderSuite = std::make_unique<Luna::RendererSuite>(device);

		SceneLoader::LoadGltf(device, _scene, "assets://Models/DamagedHelmet/DamagedHelmet.gltf");

		for (int32_t x = -3; x < 3; ++x) {
			for (int32_t z = -3; z < 3; ++z) {
				auto entity = _scene.CreateEntity();
				entity.Translate(glm::vec3(x, 0, z));
				auto& cMeshRenderer      = entity.AddComponent<Luna::MeshRendererComponent>();
				cMeshRenderer.StaticMesh = Luna::MakeHandle<Luna::StaticMesh>();
			}
		}
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

		// Add Main Render Pass.
		{
			Luna::AttachmentInfo color, depth;
			depth.Format = GetDevice().GetDefaultDepthFormat();

			auto& mainPass = _renderGraph->AddPass("Lighting", Luna::RenderGraphQueueFlagBits::Graphics);

			mainPass.AddColorOutput("Lighting-Color", color);
			mainPass.SetDepthStencilOutput("Lighting-Depth", depth);

			auto renderer = Luna::MakeHandle<SceneRenderer>(_renderContext,
			                                                *_renderSuite,
			                                                SceneRendererFlagBits::ForwardOpaque |
			                                                  SceneRendererFlagBits::ForwardTransparent |
			                                                  SceneRendererFlagBits::ForwardZPrePass,
			                                                _scene);
			mainPass.SetRenderPassInterface(renderer);
		}

		_renderGraph->SetBackbufferSource("Lighting-Color");

		_renderGraph->Bake();
		_renderGraph->InstallPhysicalBuffers(physicalBuffers);

		// _renderGraph->Log();
	}

	/*
	void RenderSceneForward(Luna::Vulkan::CommandBuffer& cmd) {
	  auto& device              = GetDevice();
	  const auto frameIndex     = device.GetFrameIndex();
	  const glm::uvec2 fbSize   = _sceneSize;
	  PushConstant pushConstant = {};

	  _sceneUBO->Bind(cmd, 0, 0);

	  const auto SetTexture = [&](uint32_t set, uint32_t binding, Texture& texture, Luna::Vulkan::ImageHandle& fallback) {
	    if (texture.Image) {
	      cmd.SetTexture(set, binding, texture.Image->Image->GetView(), texture.Sampler->Sampler);
	    } else {
	      cmd.SetTexture(set, binding, fallback->GetView(), Luna::Vulkan::StockSampler::NearestWrap);
	    }
	  };

	  cmd.SetTexture(0,
	                 1,
	                 _environment ? _environment->Irradiance->GetView() : _defaultImages.BlackCube->GetView(),
	                 Luna::Vulkan::StockSampler::LinearClamp);
	  cmd.SetTexture(0,
	                 2,
	                 _environment ? _environment->Prefiltered->GetView() : _defaultImages.BlackCube->GetView(),
	                 Luna::Vulkan::StockSampler::LinearClamp);
	  cmd.SetTexture(0,
	                 3,
	                 _environment ? _environment->BrdfLut->GetView() : _defaultImages.Black2D->GetView(),
	                 Luna::Vulkan::StockSampler::LinearClamp);

	  std::function<void(Model&, const Node*)> IterateNode = [&](Model& model, const Node* node) {
	    if (node->Mesh) {
	      const auto mesh   = node->Mesh;
	      pushConstant.Node = node->GetGlobalTransform();

	      cmd.SetVertexBinding(0, *mesh->Buffer, 0, sizeof(Vertex), vk::VertexInputRate::eVertex);
	      if (mesh->TotalIndexCount > 0) { cmd.SetIndexBuffer(*mesh->Buffer, mesh->IndexOffset, vk::IndexType::eUint32); }

	      const size_t submeshCount = mesh->Submeshes.size();
	      for (size_t i = 0; i < submeshCount; ++i) {
	        const auto& submesh  = mesh->Submeshes[i];
	        const auto* material = submesh.Material;
	        material->Update(device);
	        cmd.PushConstants(&pushConstant, 0, sizeof(PushConstant));

	        cmd.SetUniformBuffer(1, 0, *material->DataBuffer);
	        SetTexture(1, 1, *material->Albedo, _defaultImages.White2D);
	        SetTexture(1, 2, *material->Normal, _defaultImages.Normal2D);
	        SetTexture(1, 3, *material->PBR, _defaultImages.White2D);
	        SetTexture(1, 4, *material->Occlusion, _defaultImages.White2D);
	        SetTexture(1, 5, *material->Emissive, _defaultImages.Black2D);

	        if (submesh.IndexCount == 0) {
	          cmd.Draw(submesh.VertexCount, 1, submesh.FirstVertex, 0);
	        } else {
	          cmd.DrawIndexed(submesh.IndexCount, 1, submesh.FirstIndex, submesh.FirstVertex, 0);
	        }
	      }
	    }

	    for (const auto* child : node->Children) { IterateNode(model, child); }
	  };

	  auto RenderModel = [&](Model& model) {
	    LunaCmdZone(cmd, "Render Model");

	    cmd.SetProgram(_program);
	    cmd.SetVertexAttribute(0, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, Position));
	    cmd.SetVertexAttribute(1, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, Normal));
	    cmd.SetVertexAttribute(2, 0, vk::Format::eR32G32B32A32Sfloat, offsetof(Vertex, Tangent));
	    cmd.SetVertexAttribute(3, 0, vk::Format::eR32G32Sfloat, offsetof(Vertex, Texcoord0));
	    cmd.SetVertexAttribute(4, 0, vk::Format::eR32G32Sfloat, offsetof(Vertex, Texcoord1));
	    cmd.SetVertexAttribute(5, 0, vk::Format::eR32G32B32A32Sfloat, offsetof(Vertex, Color0));
	    cmd.SetVertexAttribute(6, 0, vk::Format::eR32G32B32A32Uint, offsetof(Vertex, Joints0));
	    cmd.SetVertexAttribute(7, 0, vk::Format::eR32G32B32A32Sfloat, offsetof(Vertex, Weights0));

	    for (const auto* node : model.RootNodes) { IterateNode(model, node); }
	  };
	  if (_model) { RenderModel(*_model); }

	  if (_environment) {
	    LunaCmdZone(cmd, "Render Skybox");

	    cmd.SetOpaqueState();
	    cmd.SetProgram(_programSkybox);
	    cmd.SetDepthCompareOp(vk::CompareOp::eLessOrEqual);
	    cmd.SetDepthWrite(false);
	    cmd.SetCullMode(vk::CullModeFlagBits::eFront);
	    _sceneUBO->Bind(cmd, 0, 0);
	    cmd.SetTexture(1, 0, _environment->Skybox->GetView(), Luna::Vulkan::StockSampler::LinearClamp);
	    cmd.Draw(36);
	  }
	}
	*/

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
			const glm::mat4 view       = glm::lookAt(glm::vec3(1, 0.5f, 2), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
			_renderContext.SetCamera(projection, view);
		});
	}

	void RenderScene(Luna::TaskComposer& composer) {
		_renderGraph->EnqueueRenderPasses(GetDevice(), composer);
	}

	Luna::RenderContext _renderContext;
	std::unique_ptr<Luna::RenderGraph> _renderGraph;
	std::unique_ptr<Luna::RendererSuite> _renderSuite;
	Luna::Vulkan::SwapchainConfiguration _swapchainConfig;
	bool _swapchainDirty = true;

	Luna::Scene _scene;
};

Luna::Application* Luna::CreateApplication(int argc, const char** argv) {
	return new ViewerApplication();
}
