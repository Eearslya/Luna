#include <Luna/Renderer/RenderScene.hpp>
#include <Luna/Renderer/StaticMesh.hpp>
#include <Luna/Scene/Entity.hpp>
#include <Luna/Scene/MeshRendererComponent.hpp>
#include <Luna/Scene/TransformComponent.hpp>
#include <Luna/Vulkan/Image.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace Luna {
RenderScene::RenderScene(Scene& scene) : _scene(scene) {}

void RenderScene::GatherOpaqueRenderables(VisibilityList& list) {
	const auto& registry = _scene.GetRegistry();

	auto renderables = registry.view<TransformComponent, MeshRendererComponent>();
	for (auto entityId : renderables) {
		auto [cTransform, cMeshRenderer] = renderables.get(entityId);
		if (!cMeshRenderer.StaticMesh) { continue; }

		const auto transform = cTransform.GetTransform();
		const auto submeshes = cMeshRenderer.StaticMesh->GatherOpaque();
		for (const auto& submesh : submeshes) { list.push_back({submesh, transform}); }
	}
}
}  // namespace Luna