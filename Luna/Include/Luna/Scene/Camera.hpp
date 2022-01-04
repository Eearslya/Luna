#pragma once

#include <glm/glm.hpp>

namespace Luna {
class Camera {
 public:
	Camera();
	Camera(float fovDegrees, float aspectRatio, float nearPlane = 0.01f, float farPlane = 1000.0f);

	float GetPitch() const {
		return _pitch;
	}
	const glm::mat4& GetProjection() const {
		return _projection;
	}
	const glm::vec3& GetPosition() const {
		return _position;
	}
	const glm::mat4& GetView() const {
		return _view;
	}
	float GetYaw() const {
		return _yaw;
	}

	glm::vec3 GetForward() const;
	glm::vec3 GetRight() const;
	glm::vec3 GetUp() const;

	void Accelerate(const glm::vec3& velocityDelta);
	void Move(const glm::vec3& positionDelta);
	void Rotate(float pitchDelta, float yawDelta);
	void SetAspectRatio(float aspectRatio);
	void SetFOV(float fovDegrees);
	void SetPosition(const glm::vec3& position);
	void SetRotation(float pitch, float yaw);
	void SetVelocity(const glm::vec3& velocity);
	void Update(float deltaTime);

 private:
	void RecalculateProjection();
	void RecalculateView();

	glm::mat4 _projection = glm::mat4(1.0f);
	glm::mat4 _view       = glm::mat4(1.0f);
	glm::vec3 _position   = glm::vec3(0.0f);
	glm::vec3 _velocity   = glm::vec3(0.0f);
	float _pitch          = 0.0f;
	float _yaw            = -90.0f;

	float _aspectRatio = 1.0f;
	float _fovDegrees  = 70.0f;
	float _nearPlane   = 0.01f;
	float _farPlane    = 1000.0f;
};
}  // namespace Luna
