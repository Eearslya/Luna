#pragma once

#include <Luna/Scene/Camera.hpp>

namespace Luna {
class EditorCamera : public Camera {
 public:
	float GetPitch() const {
		return _pitch;
	}
	const glm::vec3& GetPosition() const {
		return _position;
	}
	float GetYaw() const {
		return _yaw;
	}

	glm::vec3 GetForward() const;
	glm::vec3 GetRight() const;
	glm::vec3 GetUp() const;
	glm::mat4 GetView() const;

	void Move(const glm::vec3& direction);
	void Rotate(float pitchDelta, float yawDelta);
	void SetPosition(const glm::vec3& position);
	void SetRotation(float pitch, float yaw);
	void Translate(const glm::vec3& translate);

 private:
	glm::vec3 _position = glm::vec3(0.0f);
	float _pitch        = 0.0f;
	float _yaw          = -90.0f;
};
}  // namespace Luna
