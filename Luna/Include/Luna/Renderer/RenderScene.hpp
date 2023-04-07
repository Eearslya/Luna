#pragma once

#include <Luna/Renderer/Common.hpp>
#include <Luna/Scene/Scene.hpp>
#include <Luna/Utility/Frustum.hpp>
#include <Luna/Utility/Threading.hpp>

namespace Luna {
class RenderContext;

class RenderScene {
 public:
	RenderScene(Scene& scene);

	void GatherOpaqueRenderables(Luna::TaskComposer& composer, const Frustum& frustum, VisibilityList& list);

 private:
	Scene& _scene;
};
}  // namespace Luna
