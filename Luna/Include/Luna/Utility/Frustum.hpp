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

	bool Intersect(const AABB& aabb) const {
		for (auto& plane : _planes) {
			bool intersectsPlane = false;
			for (uint32_t i = 0; i < 8; ++i) {
				if (glm::dot(glm::vec4(aabb.GetCorner(i), 1.0f), plane) >= 0.0f) {
					intersectsPlane = true;
					break;
				}
			}

			if (!intersectsPlane) { return false; }
		}

		return true;
	}

	bool IntersectSphere(const AABB& aabb) const {
		const glm::vec4 center(aabb.GetCenter(), 1.0f);
		const float radius = aabb.GetRadius();

		for (auto& plane : _planes) {
			if (glm::dot(plane, center) < -radius) { return false; }
		}

		return true;
	}

	void BuildPlanes(const glm::mat4& invViewProjection) {
		constexpr static const glm::vec4 tln(-1, -1, 0, 1);
		constexpr static const glm::vec4 tlf(-1, -1, 1, 1);
		constexpr static const glm::vec4 bln(-1, 1, 0, 1);
		constexpr static const glm::vec4 blf(-1, 1, 1, 1);
		constexpr static const glm::vec4 trn(1, -1, 0, 1);
		constexpr static const glm::vec4 trf(1, -1, 1, 1);
		constexpr static const glm::vec4 brn(1, 1, 0, 1);
		constexpr static const glm::vec4 brf(1, 1, 1, 1);
		constexpr static const glm::vec4 c(0, 0, 0.5, 1);

		_invViewProjection = invViewProjection;

		const auto Project  = [](const glm::vec4& v) { return glm::vec3(v) / v.w; };
		const glm::vec3 TLN = Project(_invViewProjection * tln);
		const glm::vec3 BLN = Project(_invViewProjection * bln);
		const glm::vec3 BLF = Project(_invViewProjection * blf);
		const glm::vec3 TRN = Project(_invViewProjection * trn);
		const glm::vec3 TRF = Project(_invViewProjection * trf);
		const glm::vec3 BRN = Project(_invViewProjection * brn);
		const glm::vec3 BRF = Project(_invViewProjection * brf);

		const glm::vec3 l = glm::normalize(glm::cross(BLF - BLN, TLN - BLN));
		const glm::vec3 r = glm::normalize(glm::cross(TRF - TRN, BRN - TRN));
		const glm::vec3 n = glm::normalize(glm::cross(BLN - BRN, TRN - BRN));
		const glm::vec3 f = glm::normalize(glm::cross(TRF - BRF, BLF - BRF));
		const glm::vec3 t = glm::normalize(glm::cross(TLN - TRN, TRF - TRN));
		const glm::vec3 b = glm::normalize(glm::cross(BRF - BRN, BLN - BRN));

		_planes[0] = glm::vec4(l, -glm::dot(l, BLN));
		_planes[1] = glm::vec4(r, -glm::dot(r, TRN));
		_planes[2] = glm::vec4(n, -glm::dot(n, BRN));
		_planes[3] = glm::vec4(f, -glm::dot(f, BRF));
		_planes[4] = glm::vec4(t, -glm::dot(t, TRN));
		_planes[5] = glm::vec4(b, -glm::dot(b, BRN));

		const glm::vec4 center = _invViewProjection * c;
		for (auto& p : _planes) {
			if (glm::dot(center, p) < 0.0f) { p = -p; }
		}
	}

 private:
	glm::mat4 _invViewProjection;
	std::array<glm::vec4, 6> _planes;
};
}  // namespace Luna
