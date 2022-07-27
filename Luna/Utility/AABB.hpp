#pragma once

#include <glm/glm.hpp>

namespace Luna {
struct AABB {
	AABB() = default;
	AABB(const glm::vec3& min, const glm::vec3& max)
			: Min(glm::min(min.x, max.x), glm::min(min.y, max.y), glm::min(min.z, max.z)),
				Max(glm::max(min.x, max.x), glm::max(min.y, max.y), glm::max(min.z, max.z)) {}
	AABB(const AABB&) = default;

	AABB& Contain(const glm::vec3& p) {
		if (Valid()) {
			Min = glm::vec3(glm::min(Min.x, p.x), glm::min(Min.y, p.y), glm::min(Min.z, p.z));
			Max = glm::vec3(glm::max(Max.x, p.x), glm::max(Max.y, p.y), glm::max(Max.z, p.z));
		} else {
			Min = p;
			Max = p;
		}

		return *this;
	}
	AABB& Contain(const AABB& aabb) {
		if (aabb.Valid()) {
			Contain(aabb.Min);
			Contain(aabb.Max);
		}
		return *this;
	}
	AABB& Transform(const glm::mat4& t) {
		if (Valid()) {
			glm::vec4 min = t * glm::vec4(Min, 1.0f);
			glm::vec4 max = t * glm::vec4(Max, 1.0f);
			Min           = glm::vec3(min) / min.w;
			Max           = glm::vec3(max) / max.w;
		}

		return *this;
	}

	bool Contains(const glm::vec3& p) const {
		return p.x > Min.x && p.y > Min.y && p.z > Min.z && p.x < Max.x && p.y < Max.y && p.z < Max.z;
	}
	bool Contains(const AABB& aabb) const {
		return Contains(aabb.Min) && Contains(aabb.Max);
	}

	bool Valid() const {
		return Min.x <= Max.x && Min.y <= Max.y && Min.z <= Max.z;
	}

	glm::vec3 Min = glm::vec3(std::numeric_limits<float>::max());
	glm::vec3 Max = glm::vec3(std::numeric_limits<float>::lowest());
};
}  // namespace Luna
