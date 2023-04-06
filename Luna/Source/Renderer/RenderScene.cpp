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

void RenderScene::GatherOpaqueRenderables(const RenderContext& context, VisibilityList& list) {
	uint32_t totalCount = 0;

	const auto& registry = _scene.GetRegistry();
	const auto& frustum  = context.GetFrustum();

	auto renderables = registry.view<TransformComponent, MeshRendererComponent>();
	for (auto entityId : renderables) {
		auto [cTransform, cMeshRenderer] = renderables.get(entityId);
		if (!cMeshRenderer.StaticMesh) { continue; }

		const auto& mesh = *cMeshRenderer.StaticMesh;
		totalCount += mesh.Submeshes.size();
		if (!frustum.Intersect(mesh.Bounds)) { continue; }

		const auto transform = cTransform.GetTransform();
		const auto submeshes = cMeshRenderer.StaticMesh->GatherOpaque();
		for (const auto& submesh : submeshes) {
			if (!frustum.Intersect(submesh->Bounds)) { continue; }

			list.push_back({submesh, transform});
		}
	}
}
}  // namespace Luna
