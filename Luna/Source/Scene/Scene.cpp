#include <Luna/Scene/Entity.hpp>
#include <Luna/Scene/RelationshipComponent.hpp>
#include <Luna/Scene/Scene.hpp>
#include <Luna/Scene/TransformComponent.hpp>

namespace Luna {
Scene::Scene() {}

Scene::~Scene() noexcept {}

std::vector<Entity> Scene::GetRootEntities() {
	std::vector<Entity> entities(_rootEntities.size());
	for (size_t i = 0; i < _rootEntities.size(); ++i) { entities[i] = Entity(_rootEntities[i], *this); }

	return entities;
}

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
	entity.AddComponent<RelationshipComponent>();

	if (parent) {
		MoveEntity(entity, parent);
	} else {
		_rootEntities.push_back(entityId);
	}

	return entity;
}

void Scene::DestroyEntity(Entity entity) {
	if (entity) {
		auto& cRelationship = entity.GetComponent<RelationshipComponent>();

		Entity child = Entity(cRelationship.FirstChild, *this);
		while (child) {
			auto& cChildRel = child.GetComponent<RelationshipComponent>();
			auto next       = Entity(cChildRel.Next, *this);

			DestroyEntity(child);

			child = next;
		}

		_registry.destroy(entity);
		auto it = std::find(_rootEntities.begin(), _rootEntities.end(), entity);
		if (it != _rootEntities.end()) { _rootEntities.erase(it); }
	}
}

void Scene::MoveEntity(Entity entity, Entity newParent) {
	auto& cRelationship = entity.GetComponent<RelationshipComponent>();

	Entity parent(cRelationship.Parent, *this);
	Entity prev(cRelationship.Prev, *this);
	Entity next(cRelationship.Next, *this);

	if (prev) {
		auto& cPrev = prev.GetComponent<RelationshipComponent>();
		cPrev.Next  = next;
	}
	if (next) {
		auto& cNext = next.GetComponent<RelationshipComponent>();
		cNext.Prev  = prev;
	}
	if (parent) {
		auto& cParent = parent.GetComponent<RelationshipComponent>();
		cParent.ChildCount--;
		if (cParent.FirstChild == entity) { cParent.FirstChild = next; }
	}
	cRelationship.Parent = newParent;

	if (newParent) {
		auto& cNewParent = newParent.GetComponent<RelationshipComponent>();
		cNewParent.ChildCount++;
		Entity sibling = Entity(cNewParent.FirstChild, *this);
		if (sibling) {
			auto* cSibling = &sibling.GetComponent<RelationshipComponent>();
			while (cSibling->Next != entt::null) {
				sibling  = Entity(cSibling->Next, *this);
				cSibling = &sibling.GetComponent<RelationshipComponent>();
			}
			cSibling->Next     = entity;
			cRelationship.Prev = sibling;
		} else {
			cNewParent.FirstChild = entity;
			cRelationship.Next    = entt::null;
			cRelationship.Prev    = entt::null;
		}
	}

	if (newParent) {
		auto it = std::find(_rootEntities.begin(), _rootEntities.end(), entity);
		if (it != _rootEntities.end()) { _rootEntities.erase(it); }
	} else {
		auto it = std::find(_rootEntities.begin(), _rootEntities.end(), entity);
		if (it == _rootEntities.end()) { _rootEntities.push_back(entity); }
	}
}
}  // namespace Luna
