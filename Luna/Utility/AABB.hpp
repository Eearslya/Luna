#pragma once

#include <glm/glm.hpp>

namespace Luna {
class AABB {
 public:
	AABB();
	AABB(const glm::vec3& origin, float radius);
	AABB(const glm::vec3& min, const glm::vec3& max);
	AABB(const AABB& other);
	AABB(AABB&& other) noexcept;
	AABB& operator=(const AABB& other);
	AABB& operator=(AABB&& other) noexcept;

	const glm::vec3& GetMax() const {
		return _max;
	}
	const glm::vec3& GetMin() const {
		return _min;
	}

	void Clear();

	AABB& Contain(const glm::vec3& p);
	AABB& Contain(const AABB& aabb);
	AABB& Expand(float v);
	AABB& Expand(const glm::vec3& origin, float radius);
	AABB& Scale(const glm::vec3& scale, const glm::vec3& origin);
	AABB& Transform(const glm::mat4& t);
	AABB& Translate(const glm::vec3& t);

	bool Contains(const glm::vec3& p) const;
	bool Contains(const AABB& aabb) const;
	glm::vec3 GetCenter() const;
	glm::vec3 GetDiagonal() const;
	float GetLongestEdge() const;
	float GetShortestEdge() const;
	bool Valid() const;

 private:
	glm::vec3 _min = glm::vec3(1.0f);
	glm::vec3 _max = glm::vec3(-1.0f);
};
}  // namespace Luna
