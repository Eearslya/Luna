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
	const float fovy        = glm::radians(_fovDegrees);
	const float tanHalfFovy = glm::tan(fovy / 2.0f);
	const float tanHalfFovx = _aspectRatio * tanHalfFovy;

	_projection       = glm::mat4(0.0f);
	_projection[0][0] = 1.0f / tanHalfFovx;
	_projection[1][1] = -(1.0f / tanHalfFovy);
	_projection[2][3] = -1.0f;
	_projection[3][2] = _zNear;
}
}  // namespace Luna
