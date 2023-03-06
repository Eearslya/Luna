#include <stb_image.h>

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

#include "Model.hpp"

constexpr const char* vertex = R"VERTEX(
#version 460 core

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inUV;

layout(set = 0, binding = 0) uniform UniformData {
  mat4 Projection;
  mat4 View;
} Uniform;

layout(push_constant) uniform PushConstantData {
  mat4 Node;
} PC;

layout(location = 0) out vec2 outUV;

void main() {
  outUV = inUV;
  gl_Position = Uniform.Projection * Uniform.View * PC.Node * vec4(inPosition, 1.0f);
}
)VERTEX";

constexpr const char* fragment = R"FRAGMENT(
#version 460 core

layout(location = 0) in vec2 inUV;

layout(set = 0, binding = 1) uniform sampler2D TexAlbedo;

layout(location = 0) out vec4 outColor;

void main() {
  outColor = texture(TexAlbedo, inUV);
}
)FRAGMENT";

struct UniformBuffer {
	glm::mat4 Projection;
	glm::mat4 View;
	glm::mat4 Model;
};

struct PushConstant {
	glm::mat4 Node = glm::mat4(1.0f);
};

struct SimpleVertex {
	glm::vec3 Position;
	glm::vec2 UV;
	glm::vec3 Color;
};

class ViewerApplication : public Luna::Application {
 public:
	virtual void OnStart() override {
		auto& device = GetDevice();

		_program = device.RequestProgram(vertex, fragment);

		_model = std::make_unique<Model>(device, "deccer-cubes-main/SM_Deccer_Cubes_Textured.gltf");

		std::vector<SimpleVertex> vertices;
		vertices.push_back({{0.0f, -1.0f, 0.0f}, {0.5f, 0.0f}, {1.0f, 0.0f, 0.0f}});
		vertices.push_back({{-1.0f, 1.0f, 0.0f}, {0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}});
		vertices.push_back({{1.0f, 1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f, 0.0f}});
		const Luna::Vulkan::BufferCreateInfo bufferCI{Luna::Vulkan::BufferDomain::Device,
		                                              sizeof(SimpleVertex) * vertices.size(),
		                                              vk::BufferUsageFlagBits::eVertexBuffer};
		_vbo = device.CreateBuffer(bufferCI, vertices.data());

		int width, height, channels;
		stbi_uc* pixels = stbi_load("wall.jpg", &width, &height, &channels, 4);
		const Luna::Vulkan::ImageCreateInfo imageCI =
			Luna::Vulkan::ImageCreateInfo::Immutable2D(vk::Format::eR8G8B8A8Srgb, width, height, true);
		const Luna::Vulkan::ImageInitialData imageData{.Data = pixels};
		_texture = device.CreateImage(imageCI, &imageData);
		stbi_image_free(pixels);
	}

	virtual void OnUpdate() override {
		auto& device          = GetDevice();
		const auto frameIndex = device.GetFrameIndex();
		const auto fbSize     = GetFramebufferSize();

		UniformBuffer uniformData = {};
		uniformData.Projection = glm::perspective(glm::radians(60.0f), float(fbSize.x) / float(fbSize.y), 0.01f, 1000.0f);
		uniformData.Projection[1][1] *= -1.0f;
		uniformData.View = glm::lookAt(glm::vec3(4, 3, 10), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));

		PushConstant pushConstant = {};

		// Update Uniform Buffer
		if (_ubos.size() <= frameIndex) { _ubos.resize(frameIndex + 1); }
		auto& ubo = _ubos[frameIndex];
		if (!ubo) {
			const Luna::Vulkan::BufferCreateInfo bufferCI{
				Luna::Vulkan::BufferDomain::Host, sizeof(UniformBuffer), vk::BufferUsageFlagBits::eUniformBuffer};
			ubo = device.CreateBuffer(bufferCI);
		}
		void* uboData = ubo->Map();
		memcpy(uboData, &uniformData, sizeof(UniformBuffer));

		auto cmd                   = device.RequestCommandBuffer();
		auto rpInfo                = device.GetSwapchainRenderPass(Luna::Vulkan::SwapchainRenderPassType::Depth);
		rpInfo.ColorClearValues[0] = vk::ClearColorValue(0.36f, 0.0f, 0.63f, 1.0f);
		cmd->BeginRenderPass(rpInfo);
		cmd->SetProgram(_program);
		cmd->SetVertexBinding(0, *_vbo, 0, sizeof(SimpleVertex), vk::VertexInputRate::eVertex);
		cmd->SetVertexAttribute(0, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, Position));
		cmd->SetVertexAttribute(1, 0, vk::Format::eR32G32Sfloat, offsetof(Vertex, Texcoord0));
		cmd->SetUniformBuffer(0, 0, *ubo);

		std::function<void(Model&, const Node*)> IterateNode = [&](Model& model, const Node* node) {
			if (node->Mesh) {
				const auto mesh   = node->Mesh;
				const auto skinId = node->Skin;
				const auto* skin  = skinId >= 0 ? model.Skins[skinId].get() : nullptr;
				pushConstant.Node = model.Animate ? node->GetAnimGlobalTransform() : node->GetGlobalTransform();
				// pushConstant.Skinned = skin ? 1 : 0;

				cmd->SetVertexBinding(0, *mesh->Buffer, 0, sizeof(Vertex), vk::VertexInputRate::eVertex);
				// cmd->SetStorageBuffer(1, 0, skin ? *skin->Buffer : *defaultJointMatrices);
				if (mesh->TotalIndexCount > 0) {
					cmd->SetIndexBuffer(*mesh->Buffer, mesh->IndexOffset, vk::IndexType::eUint32);
				}

				const size_t submeshCount = mesh->Submeshes.size();
				for (size_t i = 0; i < submeshCount; ++i) {
					const auto& submesh  = mesh->Submeshes[i];
					const auto* material = submesh.Material;
					material->Update(device);
					cmd->PushConstants(&pushConstant, 0, sizeof(PushConstant));
					cmd->SetSampler(0, 4, device.RequestSampler(Luna::Vulkan::StockSampler::LinearWrap));

					// cmd->SetUniformBuffer(2, 0, *material->DataBuffer);
					cmd->SetTexture(0, 1, material->Albedo->Image->Image->GetView(), material->Albedo->Sampler->Sampler);

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
			for (const auto* node : model.RootNodes) { IterateNode(model, node); }
		};
		if (_model) { RenderModel(*_model); }

		cmd->EndRenderPass();
		device.Submit(cmd);
	}

 private:
	Luna::Vulkan::Program* _program = nullptr;
	Luna::Vulkan::ImageHandle _texture;
	Luna::Vulkan::BufferHandle _vbo;
	std::vector<Luna::Vulkan::BufferHandle> _ubos;
	std::unique_ptr<Model> _model;
};

Luna::Application* Luna::CreateApplication(int argc, const char** argv) {
	return new ViewerApplication();
}
