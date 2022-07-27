#include "AABB.hpp"

#include <glm/gtx/component_wise.hpp>

namespace Luna {
AABB::AABB() {}

AABB::AABB(const glm::vec3& origin, float radius) {
	Expand(origin, radius);
}

AABB::AABB(const glm::vec3& min, const glm::vec3& max) {
	Contain(glm::min(min, max));
	Contain(glm::max(min, max));
}

AABB::AABB(const AABB& other) {
	*this = other;
}

AABB::AABB(AABB&& other) noexcept {
	*this = std::move(other);
}

AABB& AABB::operator=(const AABB& other) {
	_min = other._min;
	_max = other._max;

	return *this;
}

AABB& AABB::operator=(AABB&& other) noexcept {
	_min = other._min;
	_max = other._max;
	other.Clear();

	return *this;
}

void AABB::Clear() {
	_min = glm::vec3(1.0f);
	_max = glm::vec3(-1.0f);
}

AABB& AABB::Contain(const glm::vec3& p) {
	if (Valid()) {
		_min = glm::min(p, _min);
		_max = glm::max(p, _max);
	} else {
		_min = p;
		_max = p;
	}

	return *this;
}

AABB& AABB::Contain(const AABB& aabb) {
	if (aabb.Valid()) {
		Contain(aabb._min);
		Contain(aabb._max);
	}

	return *this;
}

AABB& AABB::Expand(float v) {
	if (Valid()) {
		_min -= glm::vec3(v);
		_max += glm::vec3(v);
	}

	return *this;
}

AABB& AABB::Expand(const glm::vec3& origin, float radius) {
	glm::vec3 r(radius);
	if (Valid()) {
		_min = glm::min(origin - r, _min);
		_max = glm::max(origin + r, _max);
	} else {
		_min = origin - r;
		_max = origin + r;
	}

	return *this;
}

AABB& AABB::Scale(const glm::vec3& scale, const glm::vec3& origin) {
	if (Valid()) {
		_min -= origin;
		_max -= origin;
		_min *= scale;
		_max *= scale;
		_min += origin;
		_max += origin;
	}

	return *this;
}

AABB& AABB::Transform(const glm::mat4& t) {
	if (Valid()) {
		const auto min = t * glm::vec4(_min, 1.0f);
		const auto max = t * glm::vec4(_max, 1.0f);
		_min           = glm::vec3(min) / min.w;
		_max           = glm::vec3(max) / max.w;
	}

	return *this;
}

AABB& AABB::Translate(const glm::vec3& t) {
	if (Valid()) {
		_min += t;
		_max += t;
	}

	return *this;
}

bool AABB::Contains(const glm::vec3& p) const {
	return Valid() && p.x > _min.x && p.y > _min.y && p.z > _min.z && p.x < _max.x && p.y < _max.y && p.z < _max.z;
}

bool AABB::Contains(const AABB& aabb) const {
	return Valid() && aabb.Valid() && Contains(aabb._min) && Contains(aabb._max);
}

glm::vec3 AABB::GetCenter() const {
	if (Valid()) {
		const auto d = GetDiagonal();
		return _min + (d * 0.5f);
	} else {
		return glm::vec3(0.0f);
	}
}

glm::vec3 AABB::GetDiagonal() const {
	if (Valid()) {
		return _max - _min;
	} else {
		return glm::vec3(0.0f);
	}
}

float AABB::GetLongestEdge() const {
	return glm::compMax(GetDiagonal());
}

float AABB::GetShortestEdge() const {
	return glm::compMin(GetDiagonal());
}

bool AABB::Valid() const {
	return _min.x <= _max.x && _min.y <= _max.y && _min.z <= _max.z;
}
}  // namespace Luna
