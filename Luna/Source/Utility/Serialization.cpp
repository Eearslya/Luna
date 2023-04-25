#include <Luna/Utility/AABB.hpp>
#include <Luna/Utility/Serialization.hpp>

using json = nlohmann::json;

namespace glm {
void from_json(const json& j, vec3& v) {
	v.x = j.at(0).get<float>();
	v.y = j.at(1).get<float>();
	v.z = j.at(2).get<float>();
}

void to_json(json& j, const vec3& v) {
	j = json::array({v.x, v.y, v.z});
}
}  // namespace glm

namespace Luna {
void from_json(const nlohmann::json& j, AABB& aabb) {
	const auto min = j.at(0).get<glm::vec3>();
	const auto max = j.at(1).get<glm::vec3>();
	aabb           = AABB(min, max);
}

void to_json(nlohmann::json& j, const AABB& aabb) {
	j = json::array({aabb.Min(), aabb.Max()});
}
}  // namespace Luna
