#include <stb_image.h>

#include <Luna/Application/Input.hpp>
#include <Luna/Luna.hpp>
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

	void Bind(Luna::Vulkan::CommandBufferHandle& cmd, uint32_t set, uint32_t binding) {
		Flush();
		cmd->SetUniformBuffer(set, binding, *_buffers[_device.GetFrameIndex()], 0, sizeof(T));
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
		auto& device          = GetDevice();
		const auto frameIndex = device.GetFrameIndex();
		const auto fbSize     = GetFramebufferSize();

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

		auto cmd                   = device.RequestCommandBuffer();
		auto rpInfo                = device.GetSwapchainRenderPass(Luna::Vulkan::SwapchainRenderPassType::Depth);
		rpInfo.ColorClearValues[0] = vk::ClearColorValue(0.36f, 0.0f, 0.63f, 1.0f);
		cmd->BeginRenderPass(rpInfo);
		_sceneUBO->Bind(cmd, 0, 0);

		const auto SetTexture = [&](uint32_t set, uint32_t binding, Texture& texture, Luna::Vulkan::ImageHandle& fallback) {
			if (texture.Image) {
				cmd->SetTexture(set, binding, texture.Image->Image->GetView(), texture.Sampler->Sampler);
			} else {
				cmd->SetTexture(set, binding, fallback->GetView(), Luna::Vulkan::StockSampler::NearestWrap);
			}
		};

		cmd->SetTexture(0,
		                1,
		                _environment ? _environment->Irradiance->GetView() : _defaultImages.BlackCube->GetView(),
		                Luna::Vulkan::StockSampler::LinearClamp);
		cmd->SetTexture(0,
		                2,
		                _environment ? _environment->Prefiltered->GetView() : _defaultImages.BlackCube->GetView(),
		                Luna::Vulkan::StockSampler::LinearClamp);
		cmd->SetTexture(0,
		                3,
		                _environment ? _environment->BrdfLut->GetView() : _defaultImages.Black2D->GetView(),
		                Luna::Vulkan::StockSampler::LinearClamp);

		std::function<void(Model&, const Node*)> IterateNode = [&](Model& model, const Node* node) {
			if (node->Mesh) {
				const auto mesh   = node->Mesh;
				pushConstant.Node = node->GetGlobalTransform();

				cmd->SetVertexBinding(0, *mesh->Buffer, 0, sizeof(Vertex), vk::VertexInputRate::eVertex);
				if (mesh->TotalIndexCount > 0) {
					cmd->SetIndexBuffer(*mesh->Buffer, mesh->IndexOffset, vk::IndexType::eUint32);
				}

				const size_t submeshCount = mesh->Submeshes.size();
				for (size_t i = 0; i < submeshCount; ++i) {
					const auto& submesh  = mesh->Submeshes[i];
					const auto* material = submesh.Material;
					material->Update(device);
					cmd->PushConstants(&pushConstant, 0, sizeof(PushConstant));

					cmd->SetUniformBuffer(1, 0, *material->DataBuffer);
					SetTexture(1, 1, *material->Albedo, _defaultImages.White2D);
					SetTexture(1, 2, *material->Normal, _defaultImages.Normal2D);
					SetTexture(1, 3, *material->PBR, _defaultImages.White2D);
					SetTexture(1, 4, *material->Occlusion, _defaultImages.White2D);
					SetTexture(1, 5, *material->Emissive, _defaultImages.Black2D);

					if (submesh.IndexCount == 0) {
						cmd->Draw(submesh.VertexCount, 1, submesh.FirstVertex, 0);
					} else {
						cmd->DrawIndexed(submesh.IndexCount, 1, submesh.FirstIndex, submesh.FirstVertex, 0);
					}
				}
			}

			for (const auto* child : node->Children) { IterateNode(model, child); }
		};

		auto RenderModel = [&](Model& model) {
			LunaCmdZone(cmd, "Render Model");

			cmd->SetProgram(_program);
			cmd->SetVertexAttribute(0, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, Position));
			cmd->SetVertexAttribute(1, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, Normal));
			cmd->SetVertexAttribute(2, 0, vk::Format::eR32G32B32A32Sfloat, offsetof(Vertex, Tangent));
			cmd->SetVertexAttribute(3, 0, vk::Format::eR32G32Sfloat, offsetof(Vertex, Texcoord0));
			cmd->SetVertexAttribute(4, 0, vk::Format::eR32G32Sfloat, offsetof(Vertex, Texcoord1));
			cmd->SetVertexAttribute(5, 0, vk::Format::eR32G32B32A32Sfloat, offsetof(Vertex, Color0));
			cmd->SetVertexAttribute(6, 0, vk::Format::eR32G32B32A32Uint, offsetof(Vertex, Joints0));
			cmd->SetVertexAttribute(7, 0, vk::Format::eR32G32B32A32Sfloat, offsetof(Vertex, Weights0));

			for (const auto* node : model.RootNodes) { IterateNode(model, node); }
		};
		if (_model) { RenderModel(*_model); }

		if (_environment) {
			LunaCmdZone(cmd, "Render Skybox");

			cmd->SetOpaqueState();
			cmd->SetProgram(_programSkybox);
			cmd->SetDepthCompareOp(vk::CompareOp::eLessOrEqual);
			cmd->SetDepthWrite(false);
			cmd->SetCullMode(vk::CullModeFlagBits::eFront);
			_sceneUBO->Bind(cmd, 0, 0);
			cmd->SetTexture(1, 0, _environment->Skybox->GetView(), Luna::Vulkan::StockSampler::LinearClamp);
			cmd->Draw(36);
		}

		cmd->EndRenderPass();
		device.Submit(cmd);
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

	void LoadShaders() {
		auto& device = GetDevice();
		_program =
			device.RequestProgram(ReadFile("Resources/Shaders/PBR.vert.glsl"), ReadFile("Resources/Shaders/PBR.frag.glsl"));
		_programSkybox = device.RequestProgram(ReadFile("Resources/Shaders/Skybox.vert.glsl"),
		                                       ReadFile("Resources/Shaders/Skybox.frag.glsl"));
	}

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
