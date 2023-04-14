#pragma once

#include <glm/glm.hpp>

namespace Luna {
inline glm::vec3 HSVtoRGB(const glm::vec3& hsv) {
	static constexpr const glm::vec3 Kxyz(1.0f, 2.0f / 3.0f, 1.0f / 3.0f);
	static constexpr const glm::vec3 Kxxx(1.0f, 1.0f, 1.0f);
	static constexpr const glm::vec3 Kwww(3.0f, 3.0f, 3.0f);

	const glm::vec3 p = glm::abs(glm::fract(glm::vec3(hsv.x, hsv.x, hsv.x) + Kxyz) * 6.0f - Kwww);

	return hsv.z * glm::mix(Kxxx, glm::clamp(p - Kxxx, 0.0f, 1.0f), hsv.y);
}
}  // namespace Luna
