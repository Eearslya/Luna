#pragma once

#include <Luna/Common.hpp>

namespace Luna {
class Camera {
 public:
	Camera() noexcept;

	[[nodiscard]] float GetFovDegrees() const noexcept {
		return _fovDegrees;
	}
	[[nodiscard]] const glm::mat4& GetProjection() const noexcept {
		return _projection;
	}
	[[nodiscard]] float GetZFar() const noexcept {
		return _zFar;
	}
	[[nodiscard]] float GetZNear() const noexcept {
		return _zNear;
	}

	void SetPerspective(float fovDegrees, float zNear, float zFar) noexcept;
	void SetViewport(float width, float height) noexcept;

 private:
	void UpdateProjection() noexcept;

	float _aspectRatio    = 1.0f;
	float _fovDegrees     = 60.0f;
	glm::mat4 _projection = glm::mat4(1.0f);
	float _zNear          = 0.01f;
	float _zFar           = 100.0f;
};

class EditorCamera : public Camera {
 public:
	[[nodiscard]] float GetPitch() const noexcept {
		return _pitch;
	}
	[[nodiscard]] const glm::vec3& GetPosition() const noexcept {
		return _position;
	}
	[[nodiscard]] float GetYaw() const noexcept {
		return _yaw;
	}

	[[nodiscard]] glm::vec3 GetForward() const noexcept;
	[[nodiscard]] glm::vec3 GetRight() const noexcept;
	[[nodiscard]] glm::vec3 GetUp() const noexcept;
	[[nodiscard]] glm::mat4 GetView() const noexcept;

	void Move(const glm::vec3& direction) noexcept;
	void Rotate(float pitchDelta, float yawDelta) noexcept;
	void SetPosition(const glm::vec3& position) noexcept;
	void SetRotation(float pitch, float yaw) noexcept;
	void Translate(const glm::vec3& translate) noexcept;

 private:
	glm::vec3 _position = glm::vec3(0.0f);
	float _pitch        = 0.0f;
	float _yaw          = -90.0f;
};
}  // namespace Luna
