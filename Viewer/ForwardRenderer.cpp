#include "ForwardRenderer.hpp"

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

ForwardRenderer::ForwardRenderer(const Luna::RenderContext& context, Luna::Scene& scene)
		: _context(context), _scene(scene) {}

bool ForwardRenderer::GetClearColor(uint32_t attachment, vk::ClearColorValue* value) const {
	if (value) { *value = vk::ClearColorValue(0.0f, 0.0f, 0.0f, 1.0f); }
	return true;
}

bool ForwardRenderer::GetClearDepthStencil(vk::ClearDepthStencilValue* value) const {
	if (value) { *value = vk::ClearDepthStencilValue(1.0f, 0); }
	return true;
}

void ForwardRenderer::BuildRenderPass(Luna::Vulkan::CommandBuffer& cmd) {
	Luna::RenderParameters* params = cmd.AllocateTypedUniformData<Luna::RenderParameters>(0, 0, 1);
	*params                        = _context.GetRenderParameters();

	const auto& registry = _scene.GetRegistry();

	auto renderables = registry.view<Luna::MeshRendererComponent>();
	for (auto entityId : renderables) {
		auto [cMeshRenderer] = renderables.get(entityId);
		if (!cMeshRenderer.StaticMesh) { continue; }

		const auto& mesh = *cMeshRenderer.StaticMesh;
		const Luna::Entity entity(entityId, _scene);
		const auto transform = entity.GetGlobalTransform();
		ForwardPushConstant pc{transform};

		cmd.SetProgram(_context.GetShaders().PBRForward);
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
			cmd.SetSrgbTexture(1, 0, material->Albedo.Image->GetView(), material->Albedo.Sampler);

			if (submesh->IndexCount > 0) {
				cmd.DrawIndexed(submesh->IndexCount, 1, submesh->FirstIndex, submesh->FirstVertex, 0);
			} else {
				cmd.Draw(submesh->VertexCount, 1, submesh->FirstVertex, 0);
			}
		}
	}
}

void ForwardRenderer::EnqueuePrepareRenderPass(Luna::RenderGraph& graph, Luna::TaskComposer& composer) {}