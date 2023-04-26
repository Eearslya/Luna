#include <Luna/Scene/Entity.hpp>
#include <Luna/Scene/MeshRendererComponent.hpp>
#include <Luna/Scene/NameComponent.hpp>
#include <Luna/Scene/RelationshipComponent.hpp>
#include <Luna/Scene/Scene.hpp>
#include <Luna/Scene/TransformComponent.hpp>
#include <Luna/Utility/Log.hpp>
#include <Luna/Utility/Serialization.hpp>
#include <nlohmann/json.hpp>

using nlohmann::json;

namespace Luna {
Scene::Scene() {}

Scene::Scene(Scene&& other) {
	*this = std::move(other);
}

Scene& Scene::operator=(Scene&& other) {
	if (this == &other) { return *this; }

	_name         = other._name;
	_registry     = std::move(other._registry);
	_rootEntities = std::move(other._rootEntities);

	other._name.clear();
	other._registry.clear();
	other._rootEntities.clear();

	return *this;
}

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

	entity.AddComponent<NameComponent>(entityName);
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

void Scene::SetName(const std::string& name) {
	_name = name;
}

bool Scene::Deserialize(const std::string& sceneJson) {
	json sceneData = json::parse(sceneJson);

	std::vector<std::tuple<Entity, json>> relationships;

	_name                = sceneData.at("Name").get<std::string>();
	const auto& entities = sceneData.at("Entities");
	for (const auto& entityData : entities) {
		if (!entityData.contains("NameComponent") || !entityData.contains("TransformComponent") ||
		    !entityData.contains("RelationshipComponent")) {
			continue;
		}

		Entity entity = CreateEntity();
		try {
			entity.GetComponent<NameComponent>().Deserialize(entityData.at("NameComponent"));
			entity.GetComponent<TransformComponent>().Deserialize(entityData.at("TransformComponent"));
			relationships.push_back({entity, entityData.at("RelationshipComponent")});
		} catch (const std::exception& e) { DestroyEntity(entity); }

		if (entityData.contains("MeshRendererComponent")) {
			auto& cMeshRenderer = entity.AddComponent<MeshRendererComponent>();
			cMeshRenderer.Deserialize(entityData.at("MeshRendererComponent"));
		}
	}

	for (auto& [entity, relationshipData] : relationships) {}

	if (sceneData.contains("EditorCamera")) {
		const auto& camData = sceneData.at("EditorCamera");

		_editorCamera.SetPosition(camData.at("Position").get<glm::vec3>());
		_editorCamera.SetRotation(camData.at("Pitch").get<float>(), camData.at("Yaw").get<float>());
	}

	return true;
}

std::string Scene::Serialize() const {
	json sceneData;
	sceneData["Name"] = _name;

	json entitiesData = json::array();

	_registry.each([&](auto entityId) {
		Entity entity(entityId, *const_cast<Scene*>(this));
		if (!entity) { return; }

		json entityData;

		try {
			json nameData;
			entity.GetComponent<NameComponent>().Serialize(nameData);
			entityData["NameComponent"] = nameData;

			json transformData;
			entity.GetComponent<TransformComponent>().Serialize(transformData);
			entityData["TransformComponent"] = transformData;

			json relationshipData;
			const auto& cRelationship           = entity.GetComponent<RelationshipComponent>();
			entityData["RelationshipComponent"] = relationshipData;
		} catch (const std::exception& e) {
			Log::Error("Scene", "Failed to serialize required entity components.");
			return;
		}

		if (entity.HasComponent<MeshRendererComponent>()) {
			json meshRendererData;
			entity.GetComponent<MeshRendererComponent>().Serialize(meshRendererData);
			entityData["MeshRendererComponent"] = meshRendererData;
		}

		entitiesData.push_back(entityData);
	});

	sceneData["Entities"] = entitiesData;

	json camData;
	camData["Position"]       = _editorCamera.GetPosition();
	camData["Pitch"]          = _editorCamera.GetPitch();
	camData["Yaw"]            = _editorCamera.GetYaw();
	sceneData["EditorCamera"] = camData;

	return sceneData.dump();
}
}  // namespace Luna
