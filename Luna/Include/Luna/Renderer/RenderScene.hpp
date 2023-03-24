#pragma once

#include <Luna/Renderer/Common.hpp>
#include <Luna/Scene/Scene.hpp>

namespace Luna {
class RenderScene {
 public:
	RenderScene(Scene& scene);

	void GatherOpaqueRenderables(VisibilityList& list);

 private:
	Scene& _scene;
};
}  // namespace Luna
