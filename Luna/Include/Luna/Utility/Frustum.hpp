#pragma once

#include <Luna/Utility/AABB.hpp>
#include <array>
#include <glm/glm.hpp>

namespace Luna {
class Frustum {
 public:
	const std::array<glm::vec4, 6>& GetPlanes() const {
		return _planes;
	}

	bool Contains(const AABB& aabb) const;
	bool Intersect(const AABB& aabb) const;
	bool IntersectSphere(const AABB& aabb) const;

	void BuildPlanes(const glm::mat4& invViewProjection);

 private:
	glm::mat4 _invViewProjection;
	std::array<glm::vec4, 6> _planes;
};
}  // namespace Luna
