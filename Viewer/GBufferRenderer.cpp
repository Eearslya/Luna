#include "GBufferRenderer.hpp"

#include <Luna/Renderer/RenderContext.hpp>
#include <Luna/Renderer/StaticMesh.hpp>
#include <Luna/Scene/Entity.hpp>
#include <Luna/Scene/MeshRendererComponent.hpp>
#include <Luna/Scene/Scene.hpp>
#include <Luna/Scene/TransformComponent.hpp>
#include <Luna/Vulkan/CommandBuffer.hpp>
#include <Luna/Vulkan/Image.hpp>

struct ForwardPushConstant {
	glm::mat4 Model;
};

GBufferRenderer::GBufferRenderer(const Luna::RenderContext& context, Luna::Scene& scene)
		: _context(context), _scene(scene), _renderScene(scene) {}

bool GBufferRenderer::GetClearColor(uint32_t attachment, vk::ClearColorValue* value) const {
	if (value) { *value = vk::ClearColorValue(0.0f, 0.0f, 0.0f, 1.0f); }
	return true;
}

void GBufferRenderer::BuildRenderPass(Luna::Vulkan::CommandBuffer& cmd) {
	Luna::RenderParameters* params = cmd.AllocateTypedUniformData<Luna::RenderParameters>(0, 0, 1);
	*params                        = _context.GetRenderParameters();

	const auto& registry = _scene.GetRegistry();

	const auto SetSrgbTexture =
		[&](uint32_t set, uint32_t binding, const Luna::Texture& texture, const Luna::Vulkan::ImageHandle& fallback) {
			if (texture.Image) {
				cmd.SetSrgbTexture(set, binding, texture.Image->GetView());
			} else {
				cmd.SetTexture(set, binding, fallback->GetView());
			}

			if (texture.Sampler) {
				cmd.SetSampler(set, binding, texture.Sampler);
			} else {
				cmd.SetSampler(set, binding, Luna::Vulkan::StockSampler::DefaultGeometryFilterWrap);
			}
		};
	const auto SetUnormTexture =
		[&](uint32_t set, uint32_t binding, const Luna::Texture& texture, const Luna::Vulkan::ImageHandle& fallback) {
			if (texture.Image) {
				cmd.SetUnormTexture(set, binding, texture.Image->GetView());
			} else {
				cmd.SetTexture(set, binding, fallback->GetView());
			}

			if (texture.Sampler) {
				cmd.SetSampler(set, binding, texture.Sampler);
			} else {
				cmd.SetSampler(set, binding, Luna::Vulkan::StockSampler::DefaultGeometryFilterWrap);
			}
		};

	auto renderables = registry.view<Luna::MeshRendererComponent>();
	for (auto entityId : renderables) {
		auto [cMeshRenderer] = renderables.get(entityId);
		if (!cMeshRenderer.StaticMesh) { continue; }

		const auto& mesh = *cMeshRenderer.StaticMesh;
		const Luna::Entity entity(entityId, _scene);
		const auto transform = entity.GetGlobalTransform();
		ForwardPushConstant pc{transform};

		cmd.SetProgram(_context.GetShaders().PBRGBuffer);
		cmd.SetVertexBinding(0, *mesh.PositionBuffer, 0, mesh.PositionStride, vk::VertexInputRate::eVertex);
		if (mesh.IndexOffset > 0) { cmd.SetIndexBuffer(*mesh.PositionBuffer, mesh.IndexOffset, mesh.IndexType); }
		if (mesh.AttributeBuffer) {
			cmd.SetVertexBinding(1, *mesh.AttributeBuffer, 0, mesh.AttributeStride, vk::VertexInputRate::eVertex);
		}
		for (int i = 0; i < Luna::MeshAttributeTypeCount; ++i) {
			const auto& attr = mesh.Attributes[i];
			if (attr.Format != vk::Format::eUndefined) {
				cmd.SetVertexAttribute(i, i == 0 ? 0 : 1, attr.Format, attr.Offset);
			}
		}
		cmd.PushConstants(&pc, 0, sizeof(pc));

		const auto submeshes = mesh.GatherOpaque();
		for (const auto& submesh : submeshes) {
			const auto& material = mesh.Materials[submesh->MaterialIndex];

			cmd.SetCullMode(material->DualSided ? vk::CullModeFlagBits::eNone : vk::CullModeFlagBits::eBack);
			SetSrgbTexture(1, 0, material->Albedo, _context.GetDefaultImages().Black2D);
			SetUnormTexture(1, 1, material->Normal, _context.GetDefaultImages().Normal2D);

			if (submesh->IndexCount > 0) {
				cmd.DrawIndexed(submesh->IndexCount, 1, submesh->FirstIndex, submesh->FirstVertex, 0);
			} else {
				cmd.Draw(submesh->VertexCount, 1, submesh->FirstVertex, 0);
			}
		}
	}
}

void GBufferRenderer::EnqueuePrepareRenderPass(Luna::RenderGraph& graph, Luna::TaskComposer& composer) {}
