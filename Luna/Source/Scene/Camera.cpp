#include <Luna/Scene/Camera.hpp>
#include <algorithm>
#include <glm/gtx/transform.hpp>

namespace Luna {
Camera::Camera() {
	RecalculateProjection();
	RecalculateView();
}

Camera::Camera(float fovDegrees, float aspectRatio, float nearPlane, float farPlane)
		: _fovDegrees(fovDegrees), _aspectRatio(aspectRatio), _nearPlane(nearPlane), _farPlane(farPlane) {
	RecalculateProjection();
	RecalculateView();
}

glm::vec3 Camera::GetForward() const {
	return glm::vec3(cosf(glm::radians(_yaw)) * cosf(glm::radians(_pitch)),
	                 sinf(glm::radians(_pitch)),
	                 sinf(glm::radians(_yaw)) * cosf(glm::radians(_pitch)));
}

glm::vec3 Camera::GetRight() const {
	return glm::cross(GetForward(), GetUp());
}

glm::vec3 Camera::GetUp() const {
	return glm::vec3(0, 1, 0);
}

void Camera::Accelerate(const glm::vec3& velocityDelta) {
	_velocity += velocityDelta;
}

void Camera::Move(const glm::vec3& positionDelta) {
	SetPosition(_position + positionDelta);
}

void Camera::Rotate(float pitchDelta, float yawDelta) {
	SetRotation(_pitch + pitchDelta, _yaw + yawDelta);
}

void Camera::SetAspectRatio(float aspectRatio) {
	if (aspectRatio == _aspectRatio) { return; }

	_aspectRatio = aspectRatio;
	RecalculateProjection();
}

void Camera::SetClipping(float zNear, float zFar) {
	if (zNear == _nearPlane && zFar == _farPlane) { return; }

	_nearPlane = zNear;
	_farPlane  = zFar;
	RecalculateProjection();
}

void Camera::SetFOV(float fovDegrees) {
	if (fovDegrees == _fovDegrees) { return; }

	_fovDegrees = fovDegrees;
	RecalculateProjection();
}

void Camera::SetPosition(const glm::vec3& position) {
	if (position == _position) { return; }

	_position = position;
	RecalculateView();
}

void Camera::SetRotation(float pitch, float yaw) {
	if (pitch == _pitch && yaw == _yaw) { return; }

	_pitch = std::clamp(pitch, -89.0f, 89.0f);
	_yaw   = yaw;
	while (_yaw >= 360.0f) { _yaw -= 360.0f; }
	while (_yaw < 0.0f) { _yaw += 360.0f; }
	RecalculateView();
}

void Camera::SetVelocity(const glm::vec3& velocity) {
	_velocity = velocity;
}

void Camera::Update(float deltaTime) {
	if (_velocity.length()) { Move(_velocity * deltaTime); }
	if (_velocity.length() < 0.01f) { _velocity = glm::vec3(0.0f); }
}

void Camera::RecalculateProjection() {
	_projection = glm::perspective(glm::radians(_fovDegrees), _aspectRatio, _nearPlane, _farPlane);
	_projection[1][1] *= -1.0f;
}

void Camera::RecalculateView() {
	_view = glm::lookAt(_position, _position + GetForward(), glm::vec3(0.0f, 1.0f, 0.0f));
}
}  // namespace Luna
