#include <Luna/Scene/EditorCamera.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace Luna {
glm::vec3 EditorCamera::GetForward() const {
	const float pitch = glm::radians(_pitch);
	const float yaw   = glm::radians(_yaw);

	return glm::normalize(glm::vec3(glm::cos(yaw) * glm::cos(pitch), glm::sin(pitch), glm::sin(yaw) * glm::cos(pitch)));
}

glm::vec3 EditorCamera::GetRight() const {
	return glm::normalize(glm::cross(GetForward(), glm::vec3(0, 1, 0)));
}

glm::vec3 EditorCamera::GetUp() const {
	return glm::normalize(glm::cross(GetRight(), GetForward()));
}

glm::mat4 EditorCamera::GetView() const {
	return glm::lookAt(_position, _position + GetForward(), glm::vec3(0, 1, 0));
}

void EditorCamera::Move(const glm::vec3& direction) {
	Translate(GetForward() * direction.z);
	Translate(GetRight() * direction.x);
	Translate(GetUp() * direction.y);
}

void EditorCamera::Rotate(float pitchDelta, float yawDelta) {
	SetRotation(_pitch + pitchDelta, _yaw + yawDelta);
}

void EditorCamera::SetPosition(const glm::vec3& position) {
	_position = position;
}

void EditorCamera::SetRotation(float pitch, float yaw) {
	_pitch = glm::clamp(pitch, -89.0f, 89.0f);
	_yaw   = yaw;
	while (_yaw < 0.0f) { _yaw += 360.0f; }
	while (_yaw >= 360.0f) { _yaw -= 360.0f; }
}

void EditorCamera::Translate(const glm::vec3& translate) {
	SetPosition(_position + translate);
}
}  // namespace Luna
