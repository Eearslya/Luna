#pragma once

#include <Luna/Assets/Asset.hpp>
#include <Luna/Scene/EditorCamera.hpp>
#include <entt/entt.hpp>

namespace Luna {
class Entity;

class Scene final : public Asset {
	friend class ConstEntity;
	friend class Entity;

 public:
	Scene();
	Scene(const Scene&) = delete;
	Scene(Scene&&);
	void operator=(const Scene&) = delete;
	Scene& operator=(Scene&&);
	~Scene() noexcept;

	EditorCamera& GetEditorCamera() {
		return _editorCamera;
	}
	const EditorCamera& GetEditorCamera() const {
		return _editorCamera;
	}
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
	EditorCamera _editorCamera;
};
}  // namespace Luna
