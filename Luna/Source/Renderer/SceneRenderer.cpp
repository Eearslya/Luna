#include <Luna/Assets/AssetManager.hpp>
#include <Luna/Assets/Mesh.hpp>
#include <Luna/Editor/Editor.hpp>
#include <Luna/Renderer/RenderContext.hpp>
#include <Luna/Renderer/SceneRenderer.hpp>
#include <Luna/Renderer/ShaderManager.hpp>
#include <Luna/Scene/Entity.hpp>
#include <Luna/Scene/MeshRendererComponent.hpp>
#include <Luna/Scene/Scene.hpp>
#include <Luna/Scene/TransformComponent.hpp>
#include <Luna/Vulkan/CommandBuffer.hpp>
#include <Luna/Vulkan/Image.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace Luna {
SceneRenderer::SceneRenderer(const RenderContext& context) : _context(context) {}

bool SceneRenderer::GetClearColor(uint32_t attachment, vk::ClearColorValue* value) const {
	if (value) { *value = vk::ClearColorValue(0.0f, 0.0f, 0.0f, 1.0f); }

	return true;
}

bool SceneRenderer::GetClearDepthStencil(vk::ClearDepthStencilValue* value) const {
	if (value) { *value = vk::ClearDepthStencilValue(1.0f, 0); }

	return true;
}

void SceneRenderer::BuildRenderPass(Vulkan::CommandBuffer& cmd) {
	const auto& registry = Editor::GetActiveScene().GetRegistry();
	const auto& view     = registry.view<MeshRendererComponent>();
	if (view.size() == 0) { return; }

	auto shader = ShaderManager::RegisterGraphics("res://Shaders/DummyTri.vert.glsl", "res://Shaders/DummyTri.frag.glsl");
	auto variant = shader->RegisterVariant();
	auto program = variant->GetProgram();

	const glm::mat4 vp = _context.Projection() * _context.View();

	cmd.SetProgram(program);
	cmd.SetVertexAttribute(0, 0, vk::Format::eR32G32B32Sfloat, 0);
	for (const auto entityId : view) {
		const Entity entity(entityId, Editor::GetActiveScene());
		const auto transform      = entity.GetGlobalTransform();
		const auto& cMeshRenderer = entity.GetComponent<MeshRendererComponent>();
		const glm::mat4 mvp       = vp * transform;

		IntrusivePtr<Mesh> mesh = AssetManager::GetAsset<Mesh>(cMeshRenderer.MeshAsset);
		if (!mesh || !mesh->PositionBuffer) { continue; }

		cmd.SetVertexBinding(0, *mesh->PositionBuffer, 0, 12, vk::VertexInputRate::eVertex);
		cmd.SetIndexBuffer(*mesh->PositionBuffer, mesh->TotalVertexCount * 12, vk::IndexType::eUint32);
		cmd.PushConstants(glm::value_ptr(mvp), 0, sizeof(mvp));

		for (const auto& submesh : mesh->Submeshes) {
			cmd.DrawIndexed(submesh.IndexCount, 1, submesh.FirstIndex, submesh.FirstVertex, 0);
		}
	}
}
}  // namespace Luna