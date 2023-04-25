#pragma once

#include <glm/glm.hpp>

namespace Luna {
class Camera {
 public:
	Camera();

	float GetFovDegrees() const {
		return _fovDegrees;
	}
	const glm::mat4& GetProjection() const {
		return _projection;
	}
	float GetZFar() const {
		return _zFar;
	}
	float GetZNear() const {
		return _zNear;
	}

	void SetPerspective(float fovDegrees, float zNear, float zFar);
	void SetViewport(float width, float height);

 private:
	void UpdateProjection();

	float _aspectRatio    = 1.0f;
	float _fovDegrees     = 60.0f;
	glm::mat4 _projection = glm::mat4(1.0f);
	float _zNear          = 0.01f;
	float _zFar           = 100.0f;
};
}  // namespace Luna
