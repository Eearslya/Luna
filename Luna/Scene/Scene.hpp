#pragma once

#include <entt/entt.hpp>

namespace Luna {
class Entity;

class Scene {
 public:
	friend class Entity;
	friend class SceneHierarchyPanel;
	friend class SceneSerializer;

	Scene();
	~Scene() noexcept;

	entt::registry& GetRegistry() {
		return _registry;
	}
	const entt::registry& GetRegistry() const {
		return _registry;
	}

	void Clear();
	Entity CreateEntity(const std::string& name = "");
	Entity CreateChildEntity(Entity parent, const std::string& name = "");
	void DestroyEntity(Entity entity);

	Entity GetMainCamera();

 private:
	entt::registry _registry;
};
}  // namespace Luna
