#pragma once

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/euler_angles.hpp>

namespace Luna {
struct TransformComponent {
	entt::entity Parent = {};
	std::vector<entt::entity> Children;
	std::string Name   = "Entity";
	glm::vec3 Position = glm::vec3(0, 0, 0);
	glm::vec3 Rotation = glm::vec3(0, 0, 0);
	glm::vec3 Scale    = glm::vec3(1, 1, 1);

	mutable glm::mat4 CachedGlobalTransform = glm::mat4(1.0f);

	void UpdateGlobalTransform(const entt::registry& registry) const {
		glm::mat4 myTransform = glm::translate(glm::mat4(1.0f), Position);
		myTransform *= glm::mat4(glm::eulerAngleXYZ(Rotation.x, Rotation.y, Rotation.z));
		myTransform = glm::scale(myTransform, Scale);

		if (registry.valid(Parent)) {
			const auto& parentTransform = registry.get<TransformComponent>(Parent);
			CachedGlobalTransform       = parentTransform.CachedGlobalTransform * myTransform;
		} else {
			CachedGlobalTransform = myTransform;
		}

		for (const auto& child : Children) {
			if (registry.valid(child)) {
				const auto& childTransform = registry.get<TransformComponent>(child);
				childTransform.UpdateGlobalTransform(registry);
			}
		}
	}
};
}  // namespace Luna
