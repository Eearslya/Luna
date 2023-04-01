#include <Luna/Scene/Entity.hpp>
#include <Luna/Scene/RelationshipComponent.hpp>
#include <Luna/Scene/TransformComponent.hpp>

namespace Luna {
Entity::Entity(entt::entity handle, Scene& scene) : _handle(handle), _scene(&scene) {}

std::vector<Entity> Entity::GetChildren() const {
	const auto& cRelationship = GetComponent<RelationshipComponent>();
	if (cRelationship.ChildCount == 0) { return {}; }

	std::vector<Entity> children;
	Entity child = Entity(cRelationship.FirstChild, *_scene);
	while (child) {
		children.push_back(child);
		const auto& childRel = child.GetComponent<RelationshipComponent>();
		child                = Entity(childRel.Next, *_scene);
	}

	return children;
}

glm::mat4 Entity::GetGlobalTransform() const {
	Entity parent = GetParent();

	if (parent) {
		return parent.GetGlobalTransform() * GetLocalTransform();
	} else {
		return GetLocalTransform();
	}
}

glm::mat4 Entity::GetLocalTransform() const {
	return Transform().GetTransform();
}

Entity Entity::GetParent() const {
	auto& cRelationship = GetComponent<RelationshipComponent>();

	return Entity(cRelationship.Parent, *_scene);
}

void Entity::Rotate(const glm::vec3& rDelta) {
	Transform().Rotation += rDelta;
}

void Entity::Scale(const glm::vec3& sDelta) {
	Transform().Scale *= sDelta;
}

void Entity::Scale(float sDelta) {
	Transform().Scale *= sDelta;
}

void Entity::SetParent(Entity newParent) {
	_scene->MoveEntity(*this, newParent);
}

void Entity::Translate(const glm::vec3& tDelta) {
	Transform().Translation += tDelta;
}

TransformComponent& Entity::Transform() {
	return GetComponent<TransformComponent>();
}

const TransformComponent& Entity::Transform() const {
	return GetComponent<TransformComponent>();
}
}  // namespace Luna
