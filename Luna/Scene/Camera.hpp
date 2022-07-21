#pragma once

#include <glm/glm.hpp>

namespace Luna {
class Camera {
 public:
	Camera() = default;
	Camera(const glm::mat4& projection);

	float GetFovDegrees() const {
		return _fovDegrees;
	}
	const glm::mat4& GetProjection() const {
		return _projection;
	}
	float GetZNear() const {
		return _zNear;
	}
	float GetZFar() const {
		return _zFar;
	}

	void SetPerspective(float fovDegrees, float zNear, float zFar);
	void SetViewport(const glm::uvec2& viewportSize);

	float Pitch = 0.0f;
	float Yaw   = 0.0f;

 private:
	float _fovDegrees     = 70.0f;
	float _zNear          = 0.01f;
	float _zFar           = 1000.0f;
	glm::mat4 _projection = glm::mat4(1.0f);
};
}  // namespace Luna
