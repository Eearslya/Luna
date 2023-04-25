#pragma once

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

namespace Luna {
class AABB {
 public:
	AABB() = default;
	AABB(const glm::vec3& min, const glm::vec3& max) {
		_min.V4 = glm::vec4(glm::min(min, max), 1.0f);
		_max.V4 = glm::vec4(glm::max(min, max), 1.0f);
	}

	glm::vec3 GetCenter() const {
		return _min.V3 + (_max.V3 - _min.V3) * 0.5f;
	}
	glm::vec3 GetCorner(uint32_t i) const {
		return glm::vec3(i & 1 ? _max.V3.x : _min.V3.x, i & 2 ? _max.V3.y : _min.V3.y, i & 4 ? _max.V3.z : _min.V3.z);
	}
	float GetRadius() const {
		return glm::distance(_min.V3, _max.V3) * 0.5f;
	}
	const glm::vec3& Max() const {
		return _max.V3;
	}
	glm::vec4& Max4() {
		return _max.V4;
	}
	const glm::vec4& Max4() const {
		return _max.V4;
	}
	const glm::vec3& Min() const {
		return _min.V3;
	}
	glm::vec4& Min4() {
		return _min.V4;
	}
	const glm::vec4& Min4() const {
		return _min.V4;
	}

	glm::vec3 GetCoordinate(float dX, float dY, float dZ) const {
		return glm::mix(_min.V3, _max.V3, glm::vec3(dX, dY, dZ));
	}

	AABB Transform(const glm::mat4& mat) const {
		glm::vec3 m0 = glm::vec3(std::numeric_limits<float>::max());
		glm::vec3 m1 = glm::vec3(std::numeric_limits<float>::lowest());

		for (uint32_t i = 0; i < 8; ++i) {
			const auto c      = GetCorner(i);
			const glm::vec3 t = mat * glm::vec4(c, 1.0f);
			m0                = glm::min(t, m0);
			m1                = glm::max(t, m1);
		}

		return AABB(m0, m1);
	}

	void Expand(const AABB& aabb) {
		_min.V3 = glm::min(_min.V3, aabb._min.V3);
		_max.V3 = glm::max(_max.V3, aabb._max.V3);
	}

	static AABB Empty() {
		AABB aabb;
		aabb._min.V3 = glm::vec3(std::numeric_limits<float>::max());
		aabb._max.V3 = glm::vec3(std::numeric_limits<float>::lowest());

		return aabb;
	}

 private:
	union {
		glm::vec3 V3;
		glm::vec4 V4;
	} _min, _max;
};
}  // namespace Luna
