#include "Camera.hpp"

#include <glm/gtc/matrix_transform.hpp>

namespace Luna {
Camera::Camera(const glm::mat4& projection) : _projection(projection) {}

void Camera::SetPerspective(float fovDegrees, float zNear, float zFar) {
	_fovDegrees = fovDegrees;
	_zNear      = zNear;
	_zFar       = zFar;
}

void Camera::SetViewport(const glm::uvec2& viewportSize) {
	_projection =
		glm::perspectiveFov(glm::radians(_fovDegrees), float(viewportSize.x), float(viewportSize.y), _zNear, _zFar);
	_projection[1][1] *= -1.0f;
}
}  // namespace Luna
