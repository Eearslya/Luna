#pragma once

#include <Scene/Entity.hpp>
#include <memory>

class SceneHierarchyPanel {
 public:
	SceneHierarchyPanel(const std::shared_ptr<Luna::Scene>& scene);

	void Render();

 private:
	void DrawEntity(Luna::Entity entity);
	void DrawComponents(Luna::Entity entity);

	std::weak_ptr<Luna::Scene> _scene;
	Luna::Entity _selected = {};
};
