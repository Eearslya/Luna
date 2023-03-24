#include <Luna/Scene/Entity.hpp>
#include <Luna/Scene/Scene.hpp>
#include <Luna/Scene/TransformComponent.hpp>

namespace Luna {
Scene::Scene() {}

Scene::~Scene() noexcept {}

void Scene::Clear() {
	_registry.clear();
}

Entity Scene::CreateEntity(const std::string& name) {
	return CreateChildEntity({}, name);
}

Entity Scene::CreateChildEntity(Entity parent, const std::string& name) {
	auto entityId = _registry.create();
	Entity entity(entityId, *this);

	std::string entityName = name.empty() ? "Entity" : name;

	entity.AddComponent<TransformComponent>();

	if (parent) {
		MoveEntity(entity, parent);
	} else {
		_rootEntities.push_back(entityId);
	}

	return entity;
}

void Scene::DestroyEntity(Entity entity) {
	if (entity) {
		_registry.destroy(entity);
		auto it = std::find(_rootEntities.begin(), _rootEntities.end(), entity);
		if (it != _rootEntities.end()) { _rootEntities.erase(it); }
	}
}

void Scene::MoveEntity(Entity entity, Entity newParent) {}
}  // namespace Luna
