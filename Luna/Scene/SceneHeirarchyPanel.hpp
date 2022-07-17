#pragma once

#include <memory>

#include "Entity.hpp"

namespace Luna {
class SceneHeirarchyPanel {
 public:
	SceneHeirarchyPanel(const std::shared_ptr<Scene>& scene);

	void Render();

 private:
	void DrawEntity(Entity entity);
	void DrawComponents(Entity entity);

	std::weak_ptr<Scene> _scene;
	Entity _selected = {};
};
}  // namespace Luna
