#include <Luna/Scene/Camera.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace Luna {
Camera::Camera() {
	UpdateProjection();
}

void Camera::SetPerspective(float fovDegrees, float zNear, float zFar) {
	_fovDegrees = fovDegrees;
	_zNear      = zNear;
	_zFar       = zFar;
	UpdateProjection();
}

void Camera::SetViewport(float width, float height) {
	_aspectRatio = width / height;
	UpdateProjection();
}

void Camera::UpdateProjection() {
	_projection = glm::perspective(glm::radians(_fovDegrees), _aspectRatio, _zNear, _zFar);
	_projection[0].y *= -1.0f;
	_projection[1].y *= -1.0f;
	_projection[2].y *= -1.0f;
	_projection[3].y *= -1.0f;
}
}  // namespace Luna
