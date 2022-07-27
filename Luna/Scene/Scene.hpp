#pragma once

#include <entt/entt.hpp>
#include <filesystem>

namespace Luna {
class Entity;

class Scene {
 public:
	friend class Entity;
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
	void EntityMoved(Entity entity, Entity newParent);

	Entity GetMainCamera();
	std::vector<Entity> GetRootEntities();

	const std::filesystem::path& GetSceneAssetPath() const {
		return _sceneAssetPath;
	}

 private:
	std::filesystem::path _sceneAssetPath;
	entt::registry _registry;
	std::vector<entt::entity> _rootEntities;
};
}  // namespace Luna
