#include <Luna/Scene/Camera.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace Luna {
Camera::Camera() {
	UpdateProjection();
}

glm::vec3 Camera::GetForward() const {
	const float pitch = glm::radians(_pitch);
	const float yaw   = glm::radians(_yaw);

	return glm::normalize(glm::vec3(glm::cos(yaw) * glm::cos(pitch), glm::sin(pitch), glm::sin(yaw) * glm::cos(pitch)));
}

glm::vec3 Camera::GetRight() const {
	return glm::normalize(glm::cross(GetForward(), glm::vec3(0, 1, 0)));
}

glm::vec3 Camera::GetUp() const {
	return glm::normalize(glm::cross(GetRight(), GetForward()));
}

glm::mat4 Camera::GetView() const {
	return glm::lookAt(_position, _position + GetForward(), glm::vec3(0, 1, 0));
}

void Camera::Move(const glm::vec3& direction) {
	Translate(GetForward() * direction.z);
	Translate(GetRight() * direction.x);
	Translate(GetUp() * direction.y);
}

void Camera::Rotate(float pitchDelta, float yawDelta) {
	SetRotation(_pitch + pitchDelta, _yaw + yawDelta);
}

void Camera::SetPerspective(float fovDegrees, float zNear, float zFar) {
	_fovDegrees = fovDegrees;
	_zNear      = zNear;
	_zFar       = zFar;
	UpdateProjection();
}

void Camera::SetPosition(const glm::vec3& position) {
	_position = position;
}

void Camera::SetRotation(float pitch, float yaw) {
	_pitch = glm::clamp(pitch, -89.0f, 89.0f);
	_yaw   = yaw;
	while (_yaw < 0.0f) { _yaw += 360.0f; }
	while (_yaw >= 360.0f) { _yaw -= 360.0f; }
}

void Camera::SetViewport(float width, float height) {
	_aspectRatio = width / height;
	UpdateProjection();
}

void Camera::Translate(const glm::vec3& translate) {
	SetPosition(_position + translate);
}

void Camera::UpdateProjection() {
	_projection = glm::perspective(glm::radians(_fovDegrees), _aspectRatio, _zNear, _zFar);
	_projection[0].y *= -1.0f;
	_projection[1].y *= -1.0f;
	_projection[2].y *= -1.0f;
	_projection[3].y *= -1.0f;
}
}  // namespace Luna
