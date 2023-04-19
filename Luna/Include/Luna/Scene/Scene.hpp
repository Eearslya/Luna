#pragma once

#include <Luna/Assets/Asset.hpp>
#include <entt/entt.hpp>

namespace Luna {
class Entity;

class Scene final : public Asset {
	friend class Entity;

 public:
	Scene();
	Scene(const Scene&)          = delete;
	Scene(Scene&&)               = delete;
	void operator=(const Scene&) = delete;
	void operator=(Scene&&)      = delete;
	~Scene() noexcept;

	const std::string& GetName() const {
		return _name;
	}
	entt::registry& GetRegistry() {
		return _registry;
	}
	const entt::registry& GetRegistry() const {
		return _registry;
	}
	std::vector<Entity> GetRootEntities();

	void Clear();
	Entity CreateEntity(const std::string& name = "");
	Entity CreateChildEntity(Entity parent, const std::string& name = "");
	void DestroyEntity(Entity entity);
	void MoveEntity(Entity entity, Entity newParent);
	void SetName(const std::string& name);

	bool Deserialize(const std::string& sceneJson);
	std::string Serialize() const;

	static AssetType GetAssetType() {
		return AssetType::Scene;
	}

 private:
	std::string _name = "NewScene";
	entt::registry _registry;
	std::vector<entt::entity> _rootEntities;
};
}  // namespace Luna
