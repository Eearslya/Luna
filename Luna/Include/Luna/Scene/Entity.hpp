#pragma once

#include <Luna/Scene/TransformComponent.hpp>
#include <entt/entt.hpp>

namespace Luna {
enum class TransformSpace { Local, Parent, World };

class Entity {
 public:
	Entity(entt::entity entity = entt::null);

	TransformComponent& Transform() const;

	Entity GetParent() const;
	bool HasParent() const;
	bool Valid() const;

	void RotateAround(const glm::vec3& point, const glm::quat& rotation, TransformSpace space);

	const entt::entity EntityID = entt::null;

 private:
	entt::registry& _registry;
	mutable TransformComponent* _transform = nullptr;
};
}  // namespace Luna
