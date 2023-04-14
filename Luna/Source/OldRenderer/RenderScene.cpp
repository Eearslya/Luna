#include <Luna/Renderer/RenderContext.hpp>
#include <Luna/Renderer/RenderScene.hpp>
#include <Luna/Renderer/StaticMesh.hpp>
#include <Luna/Scene/Entity.hpp>
#include <Luna/Scene/MeshRendererComponent.hpp>
#include <Luna/Scene/TransformComponent.hpp>
#include <Luna/Utility/Log.hpp>
#include <Luna/Vulkan/Image.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace Luna {
RenderScene::RenderScene(Scene& scene) : _scene(scene) {}

void RenderScene::GatherOpaqueRenderables(Luna::TaskComposer& composer, const Frustum& frustum, VisibilityList& list) {
	auto& gather = composer.BeginPipelineStage();
	gather.Enqueue([this, &frustum, &list]() {
		const auto& registry = _scene.GetRegistry();
		auto renderables     = registry.view<MeshRendererComponent>();
		for (auto entityId : renderables) {
			auto cMeshRenderer = renderables.get<MeshRendererComponent>(entityId);
			if (!cMeshRenderer.StaticMesh) { continue; }

			const Entity entity(entityId, _scene);
			const auto transform = entity.GetGlobalTransform();

			const auto& mesh       = *cMeshRenderer.StaticMesh;
			const auto worldBounds = mesh.Bounds.Transform(transform);
			if (!frustum.Contains(worldBounds)) { continue; }

			const auto submeshes = mesh.GatherOpaque();
			for (const auto& submesh : submeshes) {
				const auto worldBounds = submesh->Bounds.Transform(transform);
				if (!frustum.Contains(worldBounds)) { continue; }

				list.push_back({submesh, transform});
			}
		}
	});
}
}  // namespace Luna
