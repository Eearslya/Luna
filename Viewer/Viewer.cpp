#include <dds-ktx.h>
#include <imgui.h>

#include <Luna/Luna.hpp>
#include <Luna/Renderer/Environment.hpp>
#include <Luna/Renderer/GlslCompiler.hpp>
#include <Luna/Renderer/RenderContext.hpp>
#include <Luna/Renderer/RenderGraph.hpp>
#include <Luna/Renderer/RenderPass.hpp>
#include <Luna/Renderer/RendererSuite.hpp>
#include <Luna/Scene/Camera.hpp>
#include <Luna/Scene/MeshRendererComponent.hpp>
#include <Luna/Scene/PointLightComponent.hpp>
#include <Luna/Scene/SkyLightComponent.hpp>
#include <Luna/Vulkan/ImGuiRenderer.hpp>
#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <utility>

// #include "ForwardRenderer.hpp"
// #include "GBufferRenderer.hpp"
#include "IconsFontAwesome6.h"
#include "SceneLoader.hpp"
#include "SceneRenderer.hpp"

using namespace Luna;

struct RenderConfig {
	RendererType RendererType    = RendererType::GeneralForward;
	bool ForwardDepthPrepass     = false;
	bool HdrBloom                = false;
	bool HdrBloomDynamicExposure = false;
};

struct LightingData {
	alignas(16) glm::mat4 InverseViewProjection;
	alignas(16) glm::vec3 CameraPosition;
	float IBLStrength;
	float PrefilteredMipLevels;
	uint32_t Irradiance;
	uint32_t Prefiltered;
	uint32_t Brdf;
};

struct PointLightsData {
	uint32_t PointLightCount = 0;
	Luna::PointLightData PointLights[1024];
};

class ViewerApplication : public Luna::Application {
 public:
	virtual void OnStart() override {
		auto* filesystem = Luna::Filesystem::Get();
		auto& device     = GetDevice();

		_camera.SetPosition({2, 1, 1});
		_camera.Rotate(-5, -60);

		_imGuiRenderer = Luna::MakeHandle<Luna::Vulkan::ImGuiRenderer>(GetWSI());
		_imGuiRenderer->SetRenderFunction([&]() { OnImGuiRender(); });

		StyleImGui();

		// Load Tony McMapFace LUT
		{
			auto lutFile                    = filesystem->OpenReadOnlyMapping("res://Textures/TonyMcMapface.dds");
			const void* data                = lutFile->Data();
			ddsktx_texture_info textureInfo = {};
			ddsktx_error error              = {};
			if (!ddsktx_parse(&textureInfo, data, lutFile->GetSize(), &error)) {
				Luna::Log::Error("Viewer", "Failed to load Tonemap LUT: {}", error.msg);
				abort();
			}

			Luna::Vulkan::ImageCreateInfo imageCI = {};
			imageCI.Format                        = vk::Format::eR16G16B16A16Sfloat;
			imageCI.Width                         = textureInfo.width;
			imageCI.Height                        = textureInfo.height;
			imageCI.Depth                         = textureInfo.depth;
			imageCI.Type                          = vk::ImageType::e3D;
			imageCI.Usage                         = vk::ImageUsageFlagBits::eSampled;
			imageCI.InitialLayout                 = vk::ImageLayout::eShaderReadOnlyOptimal;

			ddsktx_sub_data subData;
			ddsktx_get_sub(&textureInfo, &subData, data, lutFile->GetSize(), 0, 0, 0);

			std::vector<Luna::Vulkan::ImageInitialData> layers(textureInfo.depth);
			for (int i = 0; i < textureInfo.depth; ++i) {
				layers[i].Data = static_cast<const uint8_t*>(subData.buff) + ((subData.row_pitch_bytes * subData.height) * i);
			}

			_lutTexture = device.CreateImage(imageCI, layers.data());
		}

		auto helmet = SceneLoader::LoadGltf(device, _scene, "assets://Models/DamagedHelmet/DamagedHelmet.gltf");
		auto sponza = SceneLoader::LoadGltf(device, _scene, "assets://Models/Sponza/Sponza.gltf");
		auto cubes =
			SceneLoader::LoadGltf(device, _scene, "assets://Models/deccer-cubes-main/SM_Deccer_Cubes_Textured_Complex.gltf");

		helmet.Translate(glm::vec3(0, 0.5, 0));
		helmet.Rotate(glm::vec3(0, 15, 0));
		helmet.Scale(0.6);

		cubes.Translate(glm::vec3(-3, 1, 0));
		cubes.Rotate(glm::vec3(30, 45, 0));
		cubes.Scale(0.1);

		auto environment = Luna::MakeHandle<Luna::Environment>(device, "assets://Environments/TokyoBigSight.hdr");
		auto skyLight    = _scene.CreateEntity("Sky Light");
		skyLight.AddComponent<Luna::SkyLightComponent>(environment);

		{
			auto pointLight = _scene.CreateEntity("Point Light");
			pointLight.Translate(glm::vec3(-2, 1, -1));
			auto& pl      = pointLight.AddComponent<Luna::PointLightComponent>();
			pl.Multiplier = 0.0f;
			pl.Radius     = 2.0f;
		}
		{
			auto pointLight = _scene.CreateEntity("Point Light");
			pointLight.Translate(glm::vec3(0, 0.2f, 1));
			auto& pl      = pointLight.AddComponent<Luna::PointLightComponent>();
			pl.Multiplier = 50.0f;
			pl.Radiance   = glm::vec3(0.36f, 0.0f, 0.63f);
			pl.Radius     = 3.0f;
		}
		{
			auto pointLight = _scene.CreateEntity("Point Light");
			pointLight.Translate(glm::vec3(-3, 1.25f, 0));
			auto& pl      = pointLight.AddComponent<Luna::PointLightComponent>();
			pl.Multiplier = 5.0f;
			pl.Radiance   = glm::vec3(0.63, 0.36, 0.0);
			pl.Radius     = 2.0f;
		}

		_swapchainConfig = GetSwapchainConfig();
		OnSwapchainChanged += [&](const Luna::Vulkan::SwapchainConfiguration& config) {
			_swapchainConfig = config;
			_swapchainDirty  = true;
		};

		_renderContext = std::make_unique<Luna::RenderContext>(device);
		_renderGraph   = std::make_unique<Luna::RenderGraph>(device);
		_renderSuite   = std::make_unique<Luna::RendererSuite>(device);

		Luna::Input::OnMouseButton += [this](Luna::MouseButton button, Luna::InputAction action, Luna::InputMods mods) {
			if (button == Luna::MouseButton::Right) {
				if (_sceneActive && action == Luna::InputAction::Press) {
					_cameraControl = true;
					Luna::Input::SetCursorHidden(true);
				} else if (action == Luna::InputAction::Release) {
					_cameraControl = false;
					Luna::Input::SetCursorHidden(false);
				}
			}
		};

		Luna::Input::OnMouseMoved += [this](const glm::dvec2& pos) {
			if (_cameraControl) {
				const float sensitivity = 0.5f;
				_camera.Rotate(pos.y * sensitivity, -(pos.x * sensitivity));
			}
		};
	}

	virtual void OnUpdate(double frameTime, double elapsedTime) override {
		if (_swapchainDirty) {
			BakeRenderGraph();
			_swapchainDirty = false;
		}

		_lastFrameTime = frameTime;

		Luna::TaskComposer composer;
		_renderGraph->SetupAttachments(&GetDevice().GetSwapchainView());
		UpdateScene(composer);
		RenderScene(composer);
		auto final = composer.GetOutgoingTask();
		final->Wait();
	}

	virtual void OnImGuiRender() override {
		const double avgMs = GetAverageFrameTime(_lastFrameTime) * 1000.0;

		_imGuiRenderer->BeginDockspace();

		if (ImGui::Begin("Heirarchy")) {
			auto rootEntities = _scene.GetRootEntities();

			if (!_selectedEntity) { _selectedEntity = {}; }

			const std::function<void(Luna::Entity&)> RenderNode = [&](Luna::Entity& entity) {
				auto children      = entity.GetChildren();
				const bool leaf    = children.empty();
				const auto& name   = entity.GetName();
				const void* nodeId = reinterpret_cast<void*>(uint64_t(entity));

				ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
				if (entity == _selectedEntity) { flags |= ImGuiTreeNodeFlags_Selected; }
				if (leaf) { flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_Bullet; }

				const bool open = ImGui::TreeNodeEx(nodeId, flags, "%s", name.c_str());
				if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) { _selectedEntity = entity; }

				if (open) {
					for (auto& child : children) { RenderNode(child); }
					ImGui::TreePop();
				}
			};

			for (auto& entity : rootEntities) { RenderNode(entity); }

			if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::IsWindowHovered() && ImGui::IsWindowFocused()) {
				_selectedEntity = {};
			}
		}
		ImGui::End();

		if (ImGui::Begin("Renderer")) {
			ImGui::Text("Frame Time: %.2f (%.0f FPS)", avgMs, 1000.0 / avgMs);

			if (_renderConfig.RendererType == RendererType::GeneralForward) {
				if (ImGui::Checkbox("Z-Prepass", &_renderConfig.ForwardDepthPrepass)) { _swapchainDirty = true; }
			}

			ImGui::SliderFloat("Exposure", &_exposure, 0.01f, 10.0f);
			ImGui::Checkbox("Dynamic Exposure", &_dynamicExposure);
			ImGui::SliderFloat("IBL Strength", &_iblStrength, 0.0f, 1.0f);

			const char* view = _renderGraphView.empty() ? "Select..." : _renderGraphView.c_str();
			if (ImGui::BeginCombo("Render Graph View", view)) {
				for (uint32_t i = 0; i < _renderGraphViews.size(); ++i) {
					if (ImGui::Selectable(_renderGraphViews[i].c_str(), false)) {
						_renderGraphView = _renderGraphViews[i];
						_swapchainDirty  = true;
					}
				}
				ImGui::EndCombo();
			}
			if (!_renderGraphView.empty()) {
				if (ImGui::Button("Reset View")) {
					_renderGraphView.clear();
					_swapchainDirty = true;
				}
			}
		}
		ImGui::End();

		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
		if (ImGui::Begin("Scene")) {
			const auto windowSize = ImGui::GetContentRegionAvail();
			const glm::uvec2 winSize(windowSize.x, windowSize.y);
			if (winSize != _sceneSize) {
				_sceneSize      = glm::uvec2(glm::max(windowSize.x, 1.0f), glm::max(windowSize.y, 1.0f));
				_swapchainDirty = true;
			}

			if (_sceneImage) {
				auto& scene        = _renderGraph->GetPhysicalTextureResource(*_sceneImage);
				const auto sceneId = _imGuiRenderer->Texture(scene);
				ImGui::Image(ImTextureID(sceneId), windowSize);
			}

			_sceneActive = ImGui::IsWindowFocused() && ImGui::IsWindowHovered();
		}
		ImGui::End();
		ImGui::PopStyleVar();

		_imGuiRenderer->EndDockspace();
	}

 private:
	void AddMainPassForward(const AttachmentInfo& attachmentBase) {
		AttachmentInfo color = attachmentBase;
		AttachmentInfo depth = attachmentBase;

		depth.Format = GetDevice().GetDefaultDepthFormat();

		// If rendering with HDR bloom, we need an HDR color format.
		// We choose either B10G11R11 or R16G16B16A16 based on what's available.
		if (_renderConfig.HdrBloom) {
			if (GetDevice().IsFormatSupported(vk::Format::eB10G11R11UfloatPack32,
			                                  vk::FormatFeatureFlagBits::eColorAttachment,
			                                  vk::ImageTiling::eOptimal)) {
				color.Format = vk::Format::eB10G11R11UfloatPack32;
			} else {
				color.Format = vk::Format::eR16G16B16A16Sfloat;
			}
		}

		auto& lighting = _renderGraph->AddPass("Lighting", RenderGraphQueueFlagBits::Graphics);

		lighting.AddColorOutput("Lighting-Color", color);
		lighting.SetDepthStencilOutput("Lighting-Depth", depth);

		SceneRendererFlags flags = SceneRendererFlagBits::ForwardOpaque | SceneRendererFlagBits::ForwardTransparent;
		if (_renderConfig.ForwardDepthPrepass) { flags |= SceneRendererFlagBits::ForwardZPrePass; }

		auto renderer = MakeHandle<SceneRenderer>(*_renderContext, *_renderSuite, flags, _scene);
		lighting.SetRenderPassInterface(renderer);

		_renderGraphViews.push_back("Lighting-Color");
	}

	void BakeRenderGraph() {
		auto physicalBuffers = _renderGraph->ConsumePhysicalBuffers();

		_renderGraph->Reset();
		_renderGraphViews.clear();
		GetDevice().NextFrame();  // Release old Render Graph resources.

		// Update swapchain dimensions and format.
		const Luna::ResourceDimensions backbufferDims{.Format = _swapchainConfig.Format.format,
		                                              .Width  = _swapchainConfig.Extent.width,
		                                              .Height = _swapchainConfig.Extent.height};
		_renderGraph->SetBackbufferDimensions(backbufferDims);

		const Luna::AttachmentInfo sceneBase = {
			.SizeClass = Luna::SizeClass::Absolute, .Width = float(_sceneSize.x), .Height = float(_sceneSize.y)};

		AddMainPassForward(sceneBase);

		/*
		{
		  Luna::AttachmentInfo vis = sceneBase;
		  vis.Format               = vk::Format::eR32Uint;

		  Luna::AttachmentInfo emissive = sceneBase;
		  emissive.Format               = vk::Format::eR16G16B16A16Sfloat;
		  if (GetDevice().IsFormatSupported(vk::Format::eB10G11R11UfloatPack32,
		                                    vk::FormatFeatureFlagBits::eColorAttachment,
		                                    vk::ImageTiling::eOptimal)) {
		    emissive.Format = vk::Format::eB10G11R11UfloatPack32;
		  }

		  Luna::AttachmentInfo blur = sceneBase;
		  blur.Width                = 0.5f;
		  blur.Height               = 0.5f;
		  blur.Format               = vk::Format::eR16G16B16A16Sfloat;
		  blur.SizeClass            = Luna::SizeClass::InputRelative;
		  blur.SizeRelativeName     = "Lighting";

		  Luna::BufferInfo luminanceBufferInfo;
		  luminanceBufferInfo.Size  = 3 * sizeof(float);
		  luminanceBufferInfo.Usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eStorageBuffer;
		  auto& luminanceBuffer     = _renderGraph->GetBufferResource("Luminance-Average");
		  luminanceBuffer.SetBufferInfo(luminanceBufferInfo);

		  _renderGraphViews.clear();

		  // Add Visibility Buffer Render Pass.
		  if (false) {
		    Luna::AttachmentInfo depth;
		    depth.Format = GetDevice().GetDefaultDepthFormat();

		    auto& visibility = _renderGraph->AddPass("Visibility", Luna::RenderGraphQueueFlagBits::Graphics);

		    visibility.AddColorOutput("Visibility-Data", vis);
		    visibility.SetDepthStencilOutput("Visibility-Depth", depth);

		    visibility.SetGetClearColor([](uint32_t, vk::ClearColorValue* value) {
		      if (value) { *value = vk::ClearColorValue(0.0f, 0.0f, 0.0f, 1.0f); }
		      return true;
		    });
		    visibility.SetGetClearDepthStencil([](vk::ClearDepthStencilValue* value) {
		      if (value) { *value = vk::ClearDepthStencilValue(1.0f, 0); }
		      return true;
		    });
		    visibility.SetBuildRenderPass([&](Luna::Vulkan::CommandBuffer& cmd) {
		      uint32_t nextObject = 0;

		      Luna::RenderParameters* params = cmd.AllocateTypedUniformData<Luna::RenderParameters>(0, 0, 1);
		      *params                        = _renderContext->GetRenderParameters();

		      cmd.SetProgram(_renderContext->GetShaders().Visibility->GetProgram());
		      const auto& registry = _scene.GetRegistry();
		      auto renderables     = registry.view<Luna::MeshRendererComponent>();
		      for (auto entityId : renderables) {
		        struct VisibilityPC {
		          glm::mat4 Transform;
		          uint32_t ObjectID;
		          uint32_t Masked;
		        };

		        auto [cMeshRenderer] = renderables.get(entityId);
		        if (!cMeshRenderer.StaticMesh) { continue; }

		        const auto& mesh = *cMeshRenderer.StaticMesh;
		        const Luna::Entity entity(entityId, _scene);
		        const auto transform = entity.GetGlobalTransform();

		        cmd.SetVertexBinding(0, *mesh.PositionBuffer, 0, mesh.PositionStride, vk::VertexInputRate::eVertex);
		        cmd.SetVertexAttribute(0, 0, vk::Format::eR32G32B32Sfloat, 0);
		        uint32_t* indexBuffer = cmd.AllocateTypedIndexData<uint32_t>(mesh.Indices.size());
		        memcpy(indexBuffer, mesh.Indices.data(), mesh.Indices.size() * sizeof(uint32_t));

		        VisibilityPC pc{transform};

		        const auto submeshes = mesh.GatherOpaque();
		        for (const auto& submesh : submeshes) {
		          const auto& material = mesh.Materials[submesh->MaterialIndex];

		          pc.ObjectID = nextObject++;
		          pc.Masked   = material->AlphaMode == Luna::AlphaMode::Mask ? 1 : 0;
		          cmd.PushConstants(&pc, 0, sizeof(pc));
		          cmd.DrawIndexed(submesh->IndexCount, 1, submesh->FirstIndex, submesh->FirstVertex, 0);
		        }
		      }
		    });
		  }

		  // Add GBuffer Render Pass.
		  if (true) {
		    Luna::AttachmentInfo albedo = sceneBase.Copy().SetFormat(vk::Format::eR8G8B8A8Srgb);
		    Luna::AttachmentInfo normal = sceneBase.Copy().SetFormat(vk::Format::eR16G16Snorm);
		    Luna::AttachmentInfo pbr    = sceneBase.Copy().SetFormat(vk::Format::eR8G8B8A8Unorm);
		    Luna::AttachmentInfo depth  = sceneBase.Copy().SetFormat(GetDevice().GetDefaultDepthFormat());

		    auto& gBuffer = _renderGraph->AddPass("GBuffer", Luna::RenderGraphQueueFlagBits::Graphics);

		    gBuffer.AddColorOutput("GBuffer-Albedo", albedo);
		    gBuffer.AddColorOutput("GBuffer-Normal", normal);
		    gBuffer.AddColorOutput("GBuffer-PBR", pbr);
		    gBuffer.AddColorOutput("GBuffer-Emissive", emissive);
		    gBuffer.SetDepthStencilOutput("Depth", depth);

		    auto renderer = Luna::MakeHandle<GBufferRenderer>(*_renderContext, _scene);
		    gBuffer.SetRenderPassInterface(renderer);

		    _renderGraphViews.push_back("GBuffer-Albedo");
		    _renderGraphViews.push_back("GBuffer-Normal");
		    _renderGraphViews.push_back("GBuffer-PBR");
		    _renderGraphViews.push_back("GBuffer-Emissive");
		    _renderGraphViews.push_back("Depth");
		  }

		  // Add Lighting Render Pass.
		  if (true) {
		    auto& lighting = _renderGraph->AddPass("Lighting", Luna::RenderGraphQueueFlagBits::Graphics);

		    lighting.AddAttachmentInput("GBuffer-Albedo");
		    lighting.AddAttachmentInput("GBuffer-Normal");
		    lighting.AddAttachmentInput("GBuffer-PBR");
		    lighting.AddAttachmentInput("Depth");
		    lighting.AddColorOutput("Lighting", emissive, "GBuffer-Emissive");

		    lighting.SetBuildRenderPass([&](Luna::Vulkan::CommandBuffer& cmd) {
		      LunaCmdZone(cmd, "Deferred Lighting");

		      const auto& dims = _renderGraph->GetResourceDimensions(_renderGraph->GetTextureResource("Lighting"));

		      LightingData light          = {};
		      const auto renderParams     = _renderContext->GetRenderParameters();
		      light.CameraPosition        = renderParams.CameraPosition;
		      light.InverseViewProjection = renderParams.InvViewProjection;

		      const auto& registry = _scene.GetRegistry();
		      auto environments    = registry.view<Luna::SkyLightComponent>();
		      if (environments.size() > 0) {
		        const auto envId = environments[0];
		        const Luna::Entity env(envId, _scene);
		        const auto& skyLight = env.GetComponent<Luna::SkyLightComponent>();

		        light.IBLStrength          = _iblStrength;
		        light.Irradiance           = _renderContext->SetTexture(skyLight.Environment->Irradiance->GetView(),
		                                                      Luna::Vulkan::StockSampler::TrilinearClamp);
		        light.Prefiltered          = _renderContext->SetTexture(skyLight.Environment->Prefiltered->GetView(),
		                                                       Luna::Vulkan::StockSampler::TrilinearClamp);
		        light.PrefilteredMipLevels = skyLight.Environment->Prefiltered->GetCreateInfo().MipLevels;
		        light.Brdf                 = _renderContext->SetTexture(skyLight.Environment->BrdfLut->GetView(),
		                                                Luna::Vulkan::StockSampler::LinearClamp);
		      }

		      auto* point            = cmd.AllocateTypedUniformData<PointLightsData>(0, 0, 1);
		      point->PointLightCount = 0;
		      auto pointLights       = registry.view<Luna::PointLightComponent>();
		      for (const auto entityId : pointLights) {
		        const Luna::Entity entity(entityId, _scene);
		        const auto& pointLight                       = pointLights.get<Luna::PointLightComponent>(entityId);
		        point->PointLights[point->PointLightCount++] = pointLight.Data(entity);
		      }

		      cmd.SetBlendEnable(true);
		      cmd.SetCullMode(vk::CullModeFlagBits::eNone);
		      cmd.SetColorBlend(vk::BlendFactor::eOne, vk::BlendOp::eAdd, vk::BlendFactor::eOne);
		      cmd.SetInputAttachments(0, 1);
		      cmd.SetBindless(1, _renderContext->GetBindlessSet());
		      cmd.SetProgram(_renderContext->GetShaders().PBRDeferred->GetProgram());
		      cmd.PushConstants(&light, 0, sizeof(light));
		      cmd.Draw(3);
		    });

		    _renderGraphViews.push_back("Lighting");
		  }

		  // Add Bloom Threshold Pass.
		  if (true) {
		    Luna::AttachmentInfo threshold;
		    threshold.Format           = vk::Format::eR16G16B16A16Sfloat;
		    threshold.SizeClass        = Luna::SizeClass::InputRelative;
		    threshold.Width            = 0.5f;
		    threshold.Height           = 0.5f;
		    threshold.SizeRelativeName = "Lighting";

		    auto& bloomThreshold = _renderGraph->AddPass("BloomThreshold", Luna::RenderGraphQueueFlagBits::Graphics);

		    auto& inputRes = bloomThreshold.AddTextureInput("Lighting");
		    bloomThreshold.AddUniformBufferInput("Luminance-Average");
		    bloomThreshold.AddColorOutput("BloomThreshold", threshold);

		    bloomThreshold.SetBuildRenderPass([&](Luna::Vulkan::CommandBuffer& cmd) {
		      LunaCmdZone(cmd, "Bloom Threshold");

		      auto& input = _renderGraph->GetPhysicalTextureResource(inputRes);
		      cmd.SetTexture(0, 0, input, Luna::Vulkan::StockSampler::LinearClamp);

		      auto& lum = _renderGraph->GetPhysicalBufferResource(luminanceBuffer);
		      cmd.SetUniformBuffer(0, 1, lum);

		      cmd.SetProgram(_renderContext->GetShaders().BloomThreshold->GetProgram());
		      cmd.SetCullMode(vk::CullModeFlagBits::eNone);
		      cmd.Draw(3);
		    });

		    _renderGraphViews.push_back("BloomThreshold");
		  }

		  // Add Bloom Downsample Passes.
		  if (true) {
		    for (int i = 0; i < 4; ++i) {
		      blur.Width /= 2;
		      blur.Height /= 2;

		      const std::string inputName  = i == 0 ? "BloomThreshold" : fmt::format("BloomDownsample{}", i - 1);
		      const std::string outputName = fmt::format("BloomDownsample{}", i);

		      auto& bloomDown = _renderGraph->AddPass(outputName, Luna::RenderGraphQueueFlagBits::Graphics);

		      auto& inputRes = bloomDown.AddTextureInput(inputName);
		      bloomDown.AddColorOutput(outputName, blur);

		      bloomDown.SetBuildRenderPass([&](Luna::Vulkan::CommandBuffer& cmd) {
		        LunaCmdZone(cmd, "Bloom Downsample");

		        auto& input = _renderGraph->GetPhysicalTextureResource(inputRes);
		        cmd.SetTexture(0, 0, input, Luna::Vulkan::StockSampler::LinearClamp);

		        const glm::vec2 invTexelSize(1.0f / input.GetImage()->GetCreateInfo().Width,
		                                     1.0f / input.GetImage()->GetCreateInfo().Height);
		        cmd.PushConstants(glm::value_ptr(invTexelSize), 0, sizeof(invTexelSize));

		        cmd.SetProgram(_renderContext->GetShaders().BloomDownsample->GetProgram());
		        cmd.SetCullMode(vk::CullModeFlagBits::eNone);
		        cmd.Draw(3);
		      });

		      _renderGraphViews.push_back(outputName);
		    }
		  }

		  // Add Bloom Upsample Passes.
		  if (true) {
		    for (int i = 0; i < 3; ++i) {
		      blur.Width *= 2;
		      blur.Height *= 2;

		      const std::string inputName  = i == 0 ? "BloomDownsample3" : fmt::format("BloomUpsample{}", i - 1);
		      const std::string outputName = fmt::format("BloomUpsample{}", i);

		      auto& bloomUp = _renderGraph->AddPass(outputName, Luna::RenderGraphQueueFlagBits::Graphics);

		      auto& inputRes = bloomUp.AddTextureInput(inputName);
		      bloomUp.AddColorOutput(outputName, blur);

		      bloomUp.SetBuildRenderPass([&](Luna::Vulkan::CommandBuffer& cmd) {
		        LunaCmdZone(cmd, "Bloom Upsample");

		        auto& input = _renderGraph->GetPhysicalTextureResource(inputRes);
		        cmd.SetTexture(0, 0, input, Luna::Vulkan::StockSampler::LinearClamp);

		        const glm::vec2 invTexelSize(1.0f / input.GetImage()->GetCreateInfo().Width,
		                                     1.0f / input.GetImage()->GetCreateInfo().Height);
		        cmd.PushConstants(glm::value_ptr(invTexelSize), 0, sizeof(invTexelSize));

		        cmd.SetProgram(_renderContext->GetShaders().BloomUpsample->GetProgram());
		        cmd.SetCullMode(vk::CullModeFlagBits::eNone);
		        cmd.Draw(3);
		      });

		      _renderGraphViews.push_back(outputName);
		    }
		  }

		  // Add Average Luminance Pass.
		  if (_dynamicExposure) {
		    auto& luminance = _renderGraph->AddPass("Luminance", Luna::RenderGraphQueueFlagBits::Compute);

		    auto& input  = luminance.AddTextureInput("BloomDownsample3", vk::PipelineStageFlagBits2::eComputeShader);
		    auto& output = luminance.AddStorageOutput("Luminance-AverageUpdated", luminanceBufferInfo, "Luminance-Average");

		    luminance.SetBuildRenderPass([&](Luna::Vulkan::CommandBuffer& cmd) {
		      LunaCmdZone(cmd, "Average Luminance");

		      auto& in  = _renderGraph->GetPhysicalTextureResource(input);
		      auto& out = _renderGraph->GetPhysicalBufferResource(output);

		      struct PushConstant {
		        glm::uvec2 Size;
		        float Lerp;
		        float Minimum;
		        float Maximum;
		      };
		      PushConstant pc = {};
		      pc.Size    = glm::uvec2(in.GetImage()->GetCreateInfo().Width / 2, in.GetImage()->GetCreateInfo().Height / 2);
		      pc.Lerp    = float(1.0 - std::pow(0.5f, _lastFrameTime));
		      pc.Minimum = -3.0f;
		      pc.Maximum = 2.0f;

		      cmd.SetStorageBuffer(0, 0, out);
		      cmd.SetTexture(0, 1, in, Luna::Vulkan::StockSampler::LinearClamp);

		      cmd.PushConstants(&pc, 0, sizeof(pc));

		      cmd.SetProgram(_renderContext->GetShaders().Luminance->GetProgram());
		      cmd.Dispatch(1, 1, 1);
		    });
		  }

		  Luna::AttachmentInfo tonemapped;
		  tonemapped.SizeClass        = Luna::SizeClass::InputRelative;
		  tonemapped.SizeRelativeName = "Lighting";

		  // Add Tonemap Render Pass.
		  if (true) {
		    auto& tonemap = _renderGraph->AddPass("Tonemap", Luna::RenderGraphQueueFlagBits::Graphics);

		    auto& inputHdr   = tonemap.AddTextureInput("Lighting");
		    auto& inputBloom = tonemap.AddTextureInput("BloomUpsample2");
		    auto& luminance  = tonemap.AddUniformBufferInput("Luminance-AverageUpdated");
		    tonemap.AddColorOutput("Tonemapped", tonemapped);

		    tonemap.SetBuildRenderPass([&](Luna::Vulkan::CommandBuffer& cmd) {
		      struct PushConstant {
		        float Exposure;
		        uint32_t DynamicExposure;
		      };

		      LunaCmdZone(cmd, "Tonemapping");

		      auto& hdr   = _renderGraph->GetPhysicalTextureResource(inputHdr);
		      auto& bloom = _renderGraph->GetPhysicalTextureResource(inputBloom);
		      auto& lum   = _renderGraph->GetPhysicalBufferResource(luminance);
		      cmd.SetTexture(0, 0, hdr, Luna::Vulkan::StockSampler::LinearClamp);
		      cmd.SetTexture(0, 1, bloom, Luna::Vulkan::StockSampler::LinearClamp);
		      cmd.SetTexture(0, 2, _lutTexture->GetView(), Luna::Vulkan::StockSampler::TrilinearClamp);
		      cmd.SetUniformBuffer(0, 3, lum);

		      PushConstant pc    = {};
		      pc.Exposure        = _exposure;
		      pc.DynamicExposure = _dynamicExposure ? 1 : 0;

		      cmd.PushConstants(&pc, 0, sizeof(pc));

		      cmd.SetProgram(_renderContext->GetShaders().Tonemap->GetProgram());
		      cmd.SetCullMode(vk::CullModeFlagBits::eNone);
		      cmd.Draw(3);
		    });

		    _renderGraphViews.push_back("Tonemapped");
		  }

		  // Add Visibility Buffer Debug Render Pass.
		  if (false) {
		    Luna::AttachmentInfo visColor;

		    auto& visDebug = _renderGraph->AddPass("VisibilityDebug", Luna::RenderGraphQueueFlagBits::Graphics);

		    visDebug.AddAttachmentInput("Visibility-Data");
		    visDebug.AddColorOutput("Visibility-Debug", visColor);

		    visDebug.SetBuildRenderPass([&](Luna::Vulkan::CommandBuffer& cmd) {
		      cmd.SetProgram(_renderContext->GetShaders().VisibilityDebug->GetProgram());
		      cmd.SetInputAttachments(0, 0);
		      cmd.Draw(3);
		    });
		  }
		}
		*/

		// Add ImGui Render Pass.
		if (true) {
			Luna::AttachmentInfo ui;

			auto& imgui = _renderGraph->AddPass("ImGUI", Luna::RenderGraphQueueFlagBits::Graphics);

			Luna::RenderTextureResource* input = &imgui.AddTextureInput("Lighting-Color");
			_sceneImage                        = input;
			imgui.AddColorOutput("UI", ui);

			imgui.SetRenderPassInterface(_imGuiRenderer);
		}

		_renderGraph->SetBackbufferSource("UI");

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

		_imGuiRenderer->UpdateFontAtlas();
	}

	void UpdateScene(Luna::TaskComposer& composer) {
		auto& updates = composer.BeginPipelineStage();
		updates.Enqueue([this]() {
			_camera.SetViewport(_sceneSize.x, _sceneSize.y);

			const float camSpeed = 3.0f;
			glm::vec3 camMove(0);
			if (Luna::Input::GetKey(Luna::Key::W) == Luna::InputAction::Press) { camMove += glm::vec3(0, 0, 1); }
			if (Luna::Input::GetKey(Luna::Key::S) == Luna::InputAction::Press) { camMove -= glm::vec3(0, 0, 1); }
			if (Luna::Input::GetKey(Luna::Key::A) == Luna::InputAction::Press) { camMove -= glm::vec3(1, 0, 0); }
			if (Luna::Input::GetKey(Luna::Key::D) == Luna::InputAction::Press) { camMove += glm::vec3(1, 0, 0); }
			if (Luna::Input::GetKey(Luna::Key::R) == Luna::InputAction::Press) { camMove += glm::vec3(0, 1, 0); }
			if (Luna::Input::GetKey(Luna::Key::F) == Luna::InputAction::Press) { camMove -= glm::vec3(0, 1, 0); }
			camMove *= camSpeed * _lastFrameTime;
			_camera.Move(camMove);

			_renderContext->BeginFrame(GetDevice().GetFrameIndex());
			_renderContext->SetCamera(_camera.GetProjection(), _camera.GetView());
		});
	}

	void RenderScene(Luna::TaskComposer& composer) {
		_renderGraph->EnqueueRenderPasses(GetDevice(), composer);
	}

	double GetAverageFrameTime(double frameTime) {
		static constexpr const int AverageCount = 30;
		static double times[AverageCount]       = {0.0};
		static int count                        = 0;
		static int index                        = 0;

		times[index++] = frameTime;
		if (count < AverageCount) { ++count; }
		if (index >= AverageCount) { index = 0; }

		double total = 0.0;
		for (int i = 0; i < count; ++i) { total += times[i]; }

		return total / count;
	}

	RenderConfig _renderConfig = {};

	Luna::Camera _camera;
	bool _cameraControl = false;
	std::unique_ptr<Luna::RenderContext> _renderContext;
	std::unique_ptr<Luna::RenderGraph> _renderGraph;
	std::unique_ptr<Luna::RendererSuite> _renderSuite;
	Luna::Vulkan::SwapchainConfiguration _swapchainConfig;
	bool _swapchainDirty = true;
	Luna::IntrusivePtr<Luna::Vulkan::ImGuiRenderer> _imGuiRenderer;
	double _lastFrameTime = 0.0;

	Luna::Scene _scene;
	glm::uvec2 _sceneSize;
	Luna::RenderTextureResource* _sceneImage = nullptr;
	bool _sceneActive                        = false;
	Luna::Entity _selectedEntity             = {};

	Luna::Vulkan::ImageHandle _lutTexture;

	bool _dynamicExposure = true;
	float _exposure       = 2.5f;
	float _iblStrength    = 0.1f;

	std::string _renderGraphView = "";
	std::vector<std::string> _renderGraphViews;
};

Luna::Application* Luna::CreateApplication(int argc, const char** argv) {
	return new ViewerApplication();
}
