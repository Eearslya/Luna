#include <Luna/Renderer/RenderScene.hpp>
#include <Luna/Renderer/StaticMesh.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace Luna {
RenderScene::RenderScene(Scene& scene) : _scene(scene) {}

void RenderScene::GatherOpaqueRenderables(VisibilityList& list) {
	for (int32_t x = -3; x < 3; ++x) {
		for (int32_t z = -3; z < 3; ++z) {
			const glm::mat4 transform = glm::translate(glm::mat4(1.0f), glm::vec3(x, 0, z));
			list.push_back({MakeHandle<StaticSubmesh>(), transform});
		}
	}
}
}  // namespace Luna
