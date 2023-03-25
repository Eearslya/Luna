#include <Luna/Scene/Entity.hpp>
#include <Luna/Scene/TransformComponent.hpp>

namespace Luna {
Entity::Entity(entt::entity handle, Scene& scene) : _handle(handle), _scene(&scene) {}

glm::mat4 Entity::GetGlobalTransform() const {
	return Transform().GetTransform();
}

glm::mat4 Entity::GetLocalTransform() const {
	return Transform().GetTransform();
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
