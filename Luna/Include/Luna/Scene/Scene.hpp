#pragma once

#include <entt/entt.hpp>
#include <optional>

namespace Luna {
class Scene {
 public:
	Scene();
	~Scene() noexcept;

	entt::registry& GetRegistry() {
		return _registry;
	}

	entt::entity CreateEntity(const std::string& name, std::optional<entt::entity> parent = std::nullopt);
	void DrawSceneGraph();

 private:
	entt::registry _registry;
	entt::entity _root;
};
}  // namespace Luna
