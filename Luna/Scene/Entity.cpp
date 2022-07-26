#include "Entity.hpp"

#include "MeshComponent.hpp"
#include "RelationshipComponent.hpp"
#include "TransformComponent.hpp"

namespace Luna {
Entity::Entity(entt::entity handle, Scene& scene) : _handle(handle), _scene(&scene) {}

TransformComponent& Entity::Transform() {
	return GetComponent<TransformComponent>();
}

const TransformComponent& Entity::Transform() const {
	return GetComponent<TransformComponent>();
}

AABB Entity::GetGlobalBounds() const {
	auto myBounds       = GetLocalBounds().Transform(GetGlobalTransform());
	auto& cRelationship = GetComponent<RelationshipComponent>();

	Entity child = Entity(cRelationship.FirstChild, *_scene);
	while (child) {
		myBounds.Contain(child.GetGlobalBounds());
		auto& cChildRel = child.GetComponent<RelationshipComponent>();
		child           = Entity(cChildRel.Next, *_scene);
	}

	return myBounds;
}

AABB Entity::GetLocalBounds() const {
	if (HasComponent<MeshComponent>()) { return GetComponent<MeshComponent>().Bounds; }

	return {};
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
	auto& cTransform = GetComponent<TransformComponent>();

	return cTransform.GetTransform();
}

Entity Entity::GetParent() const {
	auto& cRelationship = GetComponent<RelationshipComponent>();

	return Entity(cRelationship.Parent, *_scene);
}

void Entity::SetParent(Entity newParent) {
	auto& cRelationship = GetComponent<RelationshipComponent>();

	Entity parent(cRelationship.Parent, *_scene);
	Entity prev(cRelationship.Prev, *_scene);
	Entity next(cRelationship.Next, *_scene);

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
		if (cParent.FirstChild == *this) { cParent.FirstChild = next; }
	}
	cRelationship.Parent = newParent;

	if (newParent) {
		auto& cNewParent = newParent.GetComponent<RelationshipComponent>();
		Entity sibling   = Entity(cNewParent.FirstChild, *_scene);
		if (sibling) {
			auto& cSibling = sibling.GetComponent<RelationshipComponent>();
			while (cSibling.Next != entt::null) {
				sibling  = Entity(cSibling.Next, *_scene);
				cSibling = sibling.GetComponent<RelationshipComponent>();
			}
			cSibling.Next      = *this;
			cRelationship.Prev = sibling;
		} else {
			cNewParent.FirstChild = *this;
			cRelationship.Next    = entt::null;
			cRelationship.Prev    = entt::null;
		}
	}
}

void Entity::SetTranslation(const glm::vec3& translation) {
	Transform().Translation = translation;
}

void Entity::Rotate(const glm::vec3& rDelta) {
	Transform().Rotation += rDelta;
}

void Entity::Scale(const glm::vec3& sDelta) {
	Transform().Scale *= sDelta;
}

void Entity::Scale(const float sDelta) {
	Scale(glm::vec3(sDelta));
}

void Entity::Translate(const glm::vec3& tDelta) {
	Transform().Translation += tDelta;
}
}  // namespace Luna
