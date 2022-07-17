#include "Scene.hpp"

#include "CameraComponent.hpp"
#include "Entity.hpp"
#include "NameComponent.hpp"
#include "RelationshipComponent.hpp"
#include "TransformComponent.hpp"

namespace Luna {
Scene::Scene() {}

Scene::~Scene() noexcept {}

Entity Scene::CreateEntity(const std::string& name) {
	return CreateChildEntity({}, name);
}

Entity Scene::CreateChildEntity(Entity parent, const std::string& name) {
	auto entityId = _registry.create();
	Entity entity(entityId, *this);

	std::string entityName = name.empty() ? "Entity" : name;
	entity.AddComponent<NameComponent>(entityName);

	entity.AddComponent<RelationshipComponent>();
	entity.AddComponent<TransformComponent>();

	if (parent) { entity.SetParent(parent); }

	return entity;
}

void Scene::DestroyEntity(Entity entity) {
	if (entity) { _registry.destroy(entity); }
}

Entity Scene::GetMainCamera() {
	auto view = _registry.view<CameraComponent>();
	for (auto entity : view) {
		auto& cCamera = view.get<CameraComponent>(entity);
		if (cCamera.Primary) { return Entity(entity, *this); }
	}

	return {};
}
}  // namespace Luna
