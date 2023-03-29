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
	vk::DeviceAddress PositionBuffer;
	vk::DeviceAddress AttributeBuffer;
};

GBufferRenderer::GBufferRenderer(Luna::RenderContext& context, Luna::Scene& scene)
		: _context(context), _scene(scene), _renderScene(scene) {}

bool GBufferRenderer::GetClearColor(uint32_t attachment, vk::ClearColorValue* value) const {
	if (value) { *value = vk::ClearColorValue(0.0f, 0.0f, 0.0f, 1.0f); }
	return true;
}

void GBufferRenderer::BuildRenderPass(Luna::Vulkan::CommandBuffer& cmd) {
	Luna::RenderParameters* params = cmd.AllocateTypedUniformData<Luna::RenderParameters>(0, 0, 1);
	*params                        = _context.GetRenderParameters();
	cmd.SetBindless(1, _context.GetBindlessSet());

	const auto& registry = _scene.GetRegistry();

	std::vector<const Luna::StaticMesh*> staticMeshes;

	auto renderables = registry.view<Luna::MeshRendererComponent>();
	for (auto entityId : renderables) {
		auto [cMeshRenderer] = renderables.get(entityId);
		if (!cMeshRenderer.StaticMesh) { continue; }

		const auto& mesh = *cMeshRenderer.StaticMesh;
		const Luna::Entity entity(entityId, _scene);
		const auto transform = entity.GetGlobalTransform();
		ForwardPushConstant pc{
			transform, mesh.PositionBuffer->GetDeviceAddress(), mesh.AttributeBuffer->GetDeviceAddress()};

		cmd.SetProgram(_context.GetShaders().PBRGBuffer);
		if (mesh.IndexOffset > 0) { cmd.SetIndexBuffer(*mesh.PositionBuffer, mesh.IndexOffset, mesh.IndexType); }
		cmd.PushConstants(&pc, 0, sizeof(pc));

		const auto submeshes = mesh.GatherOpaque();
		for (const auto& submesh : submeshes) {
			const auto& material = mesh.Materials[submesh->MaterialIndex];

			material->BindMaterial(cmd, _context, 2, 0);
			cmd.SetCullMode(material->DualSided ? vk::CullModeFlagBits::eNone : vk::CullModeFlagBits::eBack);

			if (submesh->IndexCount > 0) {
				cmd.DrawIndexed(submesh->IndexCount, 1, submesh->FirstIndex, submesh->FirstVertex, 0);
			} else {
				cmd.Draw(submesh->VertexCount, 1, submesh->FirstVertex, 0);
			}
		}
	}
}

void GBufferRenderer::EnqueuePrepareRenderPass(Luna::RenderGraph& graph, Luna::TaskComposer& composer) {}
