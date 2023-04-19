#pragma once

#include <Luna/Editor/EditorWindow.hpp>
#include <Luna/Scene/Entity.hpp>

namespace Luna {
class SceneHeirarchyWindow final : public EditorWindow {
 public:
	virtual void OnProjectChanged() override;
	virtual void Update(double deltaTime) override;

 private:
	void DrawComponents(Entity& entity);
	void DrawEntity(Entity& entity);

	Entity _selected;
};
}  // namespace Luna
