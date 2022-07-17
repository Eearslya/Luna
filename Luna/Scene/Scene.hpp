#pragma once

#include <entt/entt.hpp>

namespace Luna {
class Entity;

class Scene {
 public:
	friend class Entity;
	friend class SceneHeirarchyPanel;

	Scene();
	~Scene() noexcept;

	Entity CreateEntity(const std::string& name = "");
	Entity CreateChildEntity(Entity parent, const std::string& name = "");
	void DestroyEntity(Entity entity);

 private:
	entt::registry _registry;
};
}  // namespace Luna
