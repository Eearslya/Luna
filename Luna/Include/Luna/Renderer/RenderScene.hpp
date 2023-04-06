#pragma once

#include <Luna/Renderer/Common.hpp>
#include <Luna/Scene/Scene.hpp>

namespace Luna {
class RenderContext;

class RenderScene {
 public:
	RenderScene(Scene& scene);

	void GatherOpaqueRenderables(const RenderContext& context, VisibilityList& list);

 private:
	Scene& _scene;
};
}  // namespace Luna
