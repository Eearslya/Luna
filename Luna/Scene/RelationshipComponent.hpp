#pragma once

#include <entt/entt.hpp>

namespace Luna {
struct RelationshipComponent {
	RelationshipComponent()                             = default;
	RelationshipComponent(const RelationshipComponent&) = default;

	size_t ChildCount = 0;

	entt::entity Parent     = entt::null;
	entt::entity FirstChild = entt::null;
	entt::entity Next       = entt::null;
	entt::entity Prev       = entt::null;
};
}  // namespace Luna
