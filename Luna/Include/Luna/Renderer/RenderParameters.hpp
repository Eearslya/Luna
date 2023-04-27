#pragma once

#include <glm/glm.hpp>

namespace Luna {
struct CameraParameters {
	glm::mat4 ViewProjection;
	glm::mat4 InvViewProjection;
	glm::mat4 Projection;
	glm::mat4 InvProjection;
	glm::mat4 View;
	glm::mat4 InvView;
	glm::vec3 CameraPosition;
	float ZNear;
	float ZFar;
};
}  // namespace Luna
