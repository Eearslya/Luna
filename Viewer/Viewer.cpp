#include <imgui.h>
#include <stb_image.h>

#include <Luna/Application/Input.hpp>
#include <Luna/Luna.hpp>
#include <Luna/Renderer/RenderGraph.hpp>
#include <Luna/Renderer/RenderPass.hpp>
#include <Luna/Vulkan/Buffer.hpp>
#include <Luna/Vulkan/CommandBuffer.hpp>
#include <Luna/Vulkan/Device.hpp>
#include <Luna/Vulkan/Image.hpp>
#include <Luna/Vulkan/RenderPass.hpp>
#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <utility>

#include "Environment.hpp"
#include "Files.hpp"
#include "IconsFontAwesome6.h"
#include "Model.hpp"

struct UniformBuffer {
	glm::mat4 Projection;
	glm::mat4 View;
	glm::mat4 Model;
};

struct SceneUBO {
	glm::mat4 Projection;
	glm::mat4 View;
	glm::mat4 ViewProjection;
	glm::vec4 ViewPosition;
	glm::vec4 SunPosition;
	float Exposure;
	float Gamma;
	float PrefilteredMipLevels;
	float IBLStrength;
};

struct PushConstant {
	glm::mat4 Node = glm::mat4(1.0f);
};

template <typename T>
class UniformBufferSet {
 public:
	UniformBufferSet(Luna::Vulkan::Device& device) : _device(device) {
		const uint32_t frames = device.GetFramesInFlight();
		const Luna::Vulkan::BufferCreateInfo bufferCI{
			Luna::Vulkan::BufferDomain::Host, sizeof(T), vk::BufferUsageFlagBits::eUniformBuffer};
		for (uint32_t i = 0; i < frames; ++i) { _buffers.push_back(_device.CreateBuffer(bufferCI)); }
	}

	T& Data() {
		return _data;
	}
	const T& Data() const {
		return _data;
	}

	void Bind(Luna::Vulkan::CommandBuffer& cmd, uint32_t set, uint32_t binding) {
		Flush();
		cmd.SetUniformBuffer(set, binding, *_buffers[_device.GetFrameIndex()], 0, sizeof(T));
	}

	void Flush() {
		const auto& buffer = _buffers[_device.GetFrameIndex()];
		void* bufferData   = buffer->Map();
		memcpy(bufferData, &_data, sizeof(T));
	}

 private:
	Luna::Vulkan::Device& _device;
	std::vector<Luna::Vulkan::BufferHandle> _buffers;
	T _data;
};

class ViewerApplication : public Luna::Application {
 public:
	virtual void OnStart() override {
		auto& device = GetDevice();

		_renderGraph = std::make_unique<Luna::RenderGraph>(device);

		_swapchainConfig = GetSwapchainConfig();
		OnSwapchainChanged += [&](const Luna::Vulkan::SwapchainConfiguration& config) {
			_swapchainConfig = config;
			_swapchainDirty  = true;
		};

		StyleImGui();

		// Default Images
		{
			uint32_t pixel;
			std::array<Luna::Vulkan::ImageInitialData, 6> initialImages;
			for (int i = 0; i < 6; ++i) { initialImages[i] = Luna::Vulkan::ImageInitialData{.Data = &pixel}; }
			const Luna::Vulkan::ImageCreateInfo imageCI2D = {
				.Domain        = Luna::Vulkan::ImageDomain::Physical,
				.Width         = 1,
				.Height        = 1,
				.Depth         = 1,
				.MipLevels     = 1,
				.ArrayLayers   = 1,
				.Format        = vk::Format::eR8G8B8A8Unorm,
				.InitialLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
				.Type          = vk::ImageType::e2D,
				.Usage         = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eInputAttachment,
				.Samples       = vk::SampleCountFlagBits::e1,
			};
			const Luna::Vulkan::ImageCreateInfo imageCICube = {
				.Domain        = Luna::Vulkan::ImageDomain::Physical,
				.Width         = 1,
				.Height        = 1,
				.Depth         = 1,
				.MipLevels     = 1,
				.ArrayLayers   = 6,
				.Format        = vk::Format::eR8G8B8A8Unorm,
				.InitialLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
				.Type          = vk::ImageType::e2D,
				.Usage         = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eInputAttachment,
				.Samples       = vk::SampleCountFlagBits::e1,
				.Flags         = vk::ImageCreateFlagBits::eCubeCompatible,
			};

			pixel                    = 0xff000000;
			_defaultImages.Black2D   = device.CreateImage(imageCI2D, initialImages.data());
			_defaultImages.BlackCube = device.CreateImage(imageCICube, initialImages.data());

			pixel                 = 0xff888888;
			_defaultImages.Gray2D = device.CreateImage(imageCI2D, initialImages.data());

			pixel                   = 0xffff8888;
			_defaultImages.Normal2D = device.CreateImage(imageCI2D, initialImages.data());

			pixel                    = 0xffffffff;
			_defaultImages.White2D   = device.CreateImage(imageCI2D, initialImages.data());
			_defaultImages.WhiteCube = device.CreateImage(imageCICube, initialImages.data());
		}

		_sceneUBO = std::make_unique<UniformBufferSet<SceneUBO>>(device);

		_environment = std::make_unique<Environment>(device, "Assets/Environments/TokyoBigSight.hdr");
		_model       = std::make_unique<Model>(device, "Assets/Models/DamagedHelmet/DamagedHelmet.gltf");

		Luna::Input::OnKey += [&](Luna::Key key, Luna::InputAction action, Luna::InputMods mods) {
			if (action == Luna::InputAction::Press && key == Luna::Key::F5) { LoadShaders(); }
		};
		LoadShaders();
	}

	virtual void OnUpdate() override {
		if (_swapchainDirty) {
			BakeRenderGraph();
			_swapchainDirty = false;
		}

		auto& device = GetDevice();

		/*
		auto cmd                   = device.RequestCommandBuffer();
		auto rpInfo                = device.GetSwapchainRenderPass(Luna::Vulkan::SwapchainRenderPassType::Depth);
		rpInfo.ColorClearValues[0] = vk::ClearColorValue(0.36f, 0.0f, 0.63f, 1.0f);
		cmd->BeginRenderPass(rpInfo);
		cmd->EndRenderPass();
		device.Submit(cmd);
		*/

		Luna::TaskComposer composer;
		_renderGraph->SetupAttachments(&device.GetSwapchainView());
		_renderGraph->EnqueueRenderPasses(device, composer);
		auto final = composer.GetOutgoingTask();
		final->Wait();
	}

	virtual void OnImGuiRender() override {
		ImGui::ShowDemoWindow();

		if (ImGui::Begin("Window")) {}
		ImGui::End();
	}

 private:
	struct DefaultImages {
		Luna::Vulkan::ImageHandle Black2D;
		Luna::Vulkan::ImageHandle BlackCube;
		Luna::Vulkan::ImageHandle Gray2D;
		Luna::Vulkan::ImageHandle Normal2D;
		Luna::Vulkan::ImageHandle White2D;
		Luna::Vulkan::ImageHandle WhiteCube;
	};

	void BakeRenderGraph() {
		auto physicalBuffers = _renderGraph->ConsumePhysicalBuffers();

		_renderGraph->Reset();
		GetDevice().NextFrame();  // Release old Render Graph resources.

		const Luna::ResourceDimensions backbufferDims{.Format = _swapchainConfig.Format.format,
		                                              .Width  = _swapchainConfig.Extent.width,
		                                              .Height = _swapchainConfig.Extent.height};
		_renderGraph->SetBackbufferDimensions(backbufferDims);

		auto& mainPass = _renderGraph->AddPass("Main", Luna::RenderGraphQueueFlagBits::Graphics);
		Luna::AttachmentInfo mainColor;
		Luna::AttachmentInfo mainDepth = {.Format = GetDevice().GetDefaultDepthFormat()};
		mainPass.AddColorOutput("Main-Color", mainColor);
		mainPass.SetDepthStencilOutput("Main-Depth", mainDepth);
		mainPass.SetGetClearColor([](uint32_t, vk::ClearColorValue* value) {
			if (value) { *value = vk::ClearColorValue(0.36f, 0.0f, 0.63f, 1.0f); }
			return true;
		});
		mainPass.SetGetClearDepthStencil([](vk::ClearDepthStencilValue* value) {
			if (value) { *value = vk::ClearDepthStencilValue(1.0f, 0); }

			return true;
		});
		mainPass.SetBuildRenderPass([&](Luna::Vulkan::CommandBuffer& cmd) { RenderMainPass(cmd); });

		_renderGraph->SetBackbufferSource("Main-Color");

		_renderGraph->Bake();
		_renderGraph->InstallPhysicalBuffers(physicalBuffers);

		_renderGraph->Log();
	}

	void LoadShaders() {
		auto& device = GetDevice();
		_program =
			device.RequestProgram(ReadFile("Resources/Shaders/PBR.vert.glsl"), ReadFile("Resources/Shaders/PBR.frag.glsl"));
		_programSkybox = device.RequestProgram(ReadFile("Resources/Shaders/Skybox.vert.glsl"),
		                                       ReadFile("Resources/Shaders/Skybox.frag.glsl"));
	}

	void RenderMainPass(Luna::Vulkan::CommandBuffer& cmd) {
		auto& device              = GetDevice();
		const auto frameIndex     = device.GetFrameIndex();
		const auto fbSize         = GetFramebufferSize();
		PushConstant pushConstant = {};

		// Update Uniform Buffer
		auto& sceneData          = _sceneUBO->Data();
		sceneData.Projection     = glm::perspective(glm::radians(60.0f), float(fbSize.x) / float(fbSize.y), 0.01f, 1000.0f);
		sceneData.View           = glm::lookAt(glm::vec3(1, 0.5f, 2), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
		sceneData.ViewProjection = sceneData.Projection * sceneData.View;
		sceneData.ViewPosition   = glm::vec4(1, 0.5f, 2, 1.0f);
		sceneData.SunPosition    = glm::vec4(10.0f, 10.0f, 10.0f, 1.0f);
		sceneData.Exposure       = 4.5f;
		sceneData.Gamma          = 2.2f;
		sceneData.PrefilteredMipLevels = _environment ? _environment->Prefiltered->GetCreateInfo().MipLevels : 1;
		sceneData.IBLStrength          = _environment ? 1.0f : 0.0f;

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

	std::unique_ptr<Luna::RenderGraph> _renderGraph;
	Luna::Vulkan::SwapchainConfiguration _swapchainConfig;
	bool _swapchainDirty = true;

	Luna::Vulkan::Program* _program       = nullptr;
	Luna::Vulkan::Program* _programSkybox = nullptr;
	std::unique_ptr<Environment> _environment;
	std::unique_ptr<Model> _model;
	std::unique_ptr<UniformBufferSet<SceneUBO>> _sceneUBO;
	DefaultImages _defaultImages;
};

Luna::Application* Luna::CreateApplication(int argc, const char** argv) {
	return new ViewerApplication();
}
