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
	entt::entity GetRoot() const {
		return _root;
	}
	entt::entity GetSelectedEntity() const {
		return _selected;
	}

	entt::entity CreateEntity(const std::string& name, std::optional<entt::entity> parent = std::nullopt);
	void LoadEnvironment(const std::string& filePath);
	entt::entity LoadModel(const std::string& filePath, entt::entity parent);

	void DrawSceneGraph();

 private:
	entt::registry _registry;
	entt::entity _root     = entt::null;
	entt::entity _selected = entt::null;
};
}  // namespace Luna
