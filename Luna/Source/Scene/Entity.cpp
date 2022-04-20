#include <Luna/Graphics/Graphics.hpp>
#include <Luna/Scene/Entity.hpp>

namespace Luna {
Entity::Entity(entt::entity entity) : EntityID(entity), _registry(Graphics::Get()->GetScene().GetRegistry()) {}

TransformComponent& Entity::Transform() const {
	if (EntityID == entt::null) { throw std::runtime_error("Attempting to access transform of invalid entity!"); }
	if (!_transform) { _transform = _registry.try_get<TransformComponent>(EntityID); }

	return *_transform;
}

Entity Entity::GetParent() const {
	const auto& transform = Transform();
	return Entity(transform.Parent);
}

bool Entity::HasParent() const {
	const auto& transform = Transform();
	return _registry.valid(transform.Parent);
}

bool Entity::Valid() const {
	return _registry.valid(EntityID);
}

void Entity::RotateAround(const glm::vec3& point, const glm::quat& rotation, TransformSpace space) {
	auto& transform            = Transform();
	glm::vec3 parentSpacePoint = glm::vec3(0);
	glm::quat oldRotation      = transform.Rotation;

	switch (space) {
		case TransformSpace::Local:
			parentSpacePoint   = transform.GetLocalTransform() * glm::vec4(point, 0.0f);
			transform.Rotation = glm::normalize(transform.Rotation * rotation);
			break;

		case TransformSpace::Parent:
			parentSpacePoint   = point;
			transform.Rotation = glm::normalize(transform.Rotation * rotation);
			break;

		case TransformSpace::World:
			if (!HasParent()) {
				parentSpacePoint   = point;
				transform.Rotation = glm::normalize(transform.Rotation * rotation);
			} else {
				parentSpacePoint = glm::inverse(GetParent().Transform().CachedGlobalTransform) * glm::vec4(point, 0.0f);
				const glm::quat worldRotation = transform.CachedGlobalRotation;
				transform.Rotation            = transform.Rotation * glm::inverse(worldRotation) * rotation * worldRotation;
			}
			break;
	}

	const glm::vec3 oldRelativePos = glm::inverse(oldRotation) * (transform.Position - parentSpacePoint);
	// transform.Position             = transform.Rotation * oldRelativePos + parentSpacePoint;
	transform.UpdateGlobalTransform(_registry);
}

void Entity::SetLocalPosition(const glm::vec3& pos) {
	auto& transform    = Transform();
	transform.Position = pos;
	transform.UpdateGlobalTransform(_registry);
}
}  // namespace Luna
