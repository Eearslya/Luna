#include <Luna/Renderer/Camera.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace Luna {
Camera::Camera() noexcept {
	UpdateProjection();
}

void Camera::SetPerspective(float fovDegrees, float zNear, float zFar) noexcept {
	Log::Assert(zNear > 0.0f, "Camera", "Near Plane must be greater than 0");

	_fovDegrees = fovDegrees;
	_zNear      = zNear;
	_zFar       = zFar;
	UpdateProjection();
}

void Camera::SetViewport(float width, float height) noexcept {
	Log::Assert(width > 0.0f && height > 0.0f, "Camera", "Viewport size must be greater than 0");

	_aspectRatio = width / height;
	UpdateProjection();
}

void Camera::UpdateProjection() noexcept {
	const float fovy        = glm::radians(_fovDegrees);
	const float tanHalfFovy = glm::tan(fovy / 2.0f);
	const float tanHalfFovx = _aspectRatio * tanHalfFovy;

	_projection       = glm::mat4(0.0f);
	_projection[0][0] = 1.0f / tanHalfFovx;
	_projection[1][1] = 1.0f / tanHalfFovy;
	_projection[2][3] = -1.0f;
	_projection[3][2] = _zNear;
}

glm::vec3 EditorCamera::GetForward() const noexcept {
	const float pitch = glm::radians(_pitch);
	const float yaw   = glm::radians(_yaw);

	return glm::normalize(glm::vec3(glm::cos(yaw) * glm::cos(pitch), glm::sin(pitch), glm::sin(yaw) * glm::cos(pitch)));
}

glm::vec3 EditorCamera::GetRight() const noexcept {
	return glm::normalize(glm::cross(GetForward(), glm::vec3(0, 1, 0)));
}

glm::vec3 EditorCamera::GetUp() const noexcept {
	return glm::normalize(glm::cross(GetRight(), GetForward()));
}

glm::mat4 EditorCamera::GetView() const noexcept {
	return glm::lookAt(_position, _position + GetForward(), glm::vec3(0, 1, 0));
}

void EditorCamera::Move(const glm::vec3& direction) noexcept {
	Translate(GetForward() * direction.z);
	Translate(GetRight() * direction.x);
	Translate(GetUp() * direction.y);
}

void EditorCamera::Rotate(float pitchDelta, float yawDelta) noexcept {
	SetRotation(_pitch + pitchDelta, _yaw + yawDelta);
}

void EditorCamera::SetPosition(const glm::vec3& position) noexcept {
	_position = position;
}

void EditorCamera::SetRotation(float pitch, float yaw) noexcept {
	_pitch = glm::clamp(pitch, -89.0f, 89.0f);
	_yaw   = yaw;
	while (_yaw < 0.0f) { _yaw += 360.0f; }
	while (_yaw >= 360.0f) { _yaw -= 360.0f; }
}

void EditorCamera::Translate(const glm::vec3& translate) noexcept {
	SetPosition(_position + translate);
}
}  // namespace Luna
