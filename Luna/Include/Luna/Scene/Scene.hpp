#pragma once

#include <entt/entt.hpp>

namespace Luna {
class Entity;

class Scene {
	friend class Entity;

 public:
	Scene();
	Scene(const Scene&)          = delete;
	Scene(Scene&&)               = delete;
	void operator=(const Scene&) = delete;
	void operator=(Scene&&)      = delete;
	~Scene() noexcept;

	void Clear();
	Entity CreateEntity(const std::string& name = "");
	Entity CreateChildEntity(Entity parent, const std::string& name = "");
	void DestroyEntity(Entity entity);
	void MoveEntity(Entity entity, Entity newParent);

 private:
	entt::registry _registry;
	std::vector<entt::entity> _rootEntities;
};
}  // namespace Luna
