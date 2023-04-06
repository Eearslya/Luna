#pragma once

#include <glm/glm.hpp>

namespace Luna {
class Camera {
 public:
	Camera();

	float GetFovDegrees() const {
		return _fovDegrees;
	}
	float GetPitch() const {
		return _pitch;
	}
	const glm::vec3& GetPosition() const {
		return _position;
	}
	const glm::mat4& GetProjection() const {
		return _projection;
	}
	float GetYaw() const {
		return _yaw;
	}
	float GetZFar() const {
		return _zFar;
	}
	float GetZNear() const {
		return _zNear;
	}

	glm::vec3 GetForward() const;
	glm::vec3 GetRight() const;
	glm::vec3 GetUp() const;
	glm::mat4 GetView() const;

	void Move(const glm::vec3& direction);
	void Rotate(float pitchDelta, float yawDelta);
	void SetPerspective(float fovDegrees, float zNear, float zFar);
	void SetPosition(const glm::vec3& position);
	void SetRotation(float pitch, float yaw);
	void SetViewport(float width, float height);
	void Translate(const glm::vec3& translate);

 private:
	void UpdateProjection();

	float _aspectRatio = 1.0f;
	float _fovDegrees  = 60.0f;
	float _pitch       = 0.0f;
	glm::vec3 _position;
	glm::mat4 _projection = glm::mat4(1.0f);
	float _yaw            = -90.0f;
	float _zNear          = 0.01f;
	float _zFar           = 100.0f;
};
}  // namespace Luna
