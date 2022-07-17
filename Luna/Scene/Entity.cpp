#include "Entity.hpp"

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

	if (newParent) {
		auto& cNewParent = parent.GetComponent<RelationshipComponent>();
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
}  // namespace Luna
