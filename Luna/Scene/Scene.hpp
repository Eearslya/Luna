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

	Entity GetMainCamera();

	const std::filesystem::path& GetSceneAssetPath() const {
		return _sceneAssetPath;
	}

 private:
	std::filesystem::path _sceneAssetPath;
	entt::registry _registry;
};
}  // namespace Luna
