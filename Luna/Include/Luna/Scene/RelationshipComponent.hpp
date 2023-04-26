#pragma once

#include <Luna/Scene/Component.hpp>
#include <entt/entt.hpp>

namespace Luna {
struct RelationshipComponent final : public Component {
	RelationshipComponent()                             = default;
	RelationshipComponent(const RelationshipComponent&) = default;

	size_t ChildCount = 0;

	entt::entity Parent     = entt::null;
	entt::entity FirstChild = entt::null;
	entt::entity Next       = entt::null;
	entt::entity Prev       = entt::null;

	virtual bool Deserialize(const nlohmann::json& data) override {
		// RelationshipComponent requires special handling by Scene.
		return true;
	}

	virtual void Serialize(nlohmann::json& data) const override {
		// RelationshipComponent requires special handling by Scene.
	}
};
}  // namespace Luna
