#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/euler_angles.hpp>

namespace Luna {
struct TransformComponent {
	TransformComponent()                          = default;
	TransformComponent(const TransformComponent&) = default;

	glm::mat4 GetTransform() const {
		glm::mat4 matrix = glm::translate(glm::mat4(1.0f), Translation);
		matrix *= glm::mat4(glm::quat(glm::radians(Rotation)));
		matrix = glm::scale(matrix, Scale);

		return matrix;
	}

	glm::vec3 Translation = glm::vec3(0.0f, 0.0f, 0.0f);
	glm::vec3 Rotation    = glm::vec3(0.0f, 0.0f, 0.0f);
	glm::vec3 Scale       = glm::vec3(1.0f, 1.0f, 1.0f);

	bool LockScale = true;
};
}  // namespace Luna
