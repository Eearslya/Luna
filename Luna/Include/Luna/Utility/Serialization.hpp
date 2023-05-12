#pragma once

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

namespace glm {
void from_json(const nlohmann::json& j, uvec2& v);
void to_json(nlohmann::json& j, const uvec2& v);
void from_json(const nlohmann::json& j, vec3& v);
void to_json(nlohmann::json& j, const vec3& v);
void from_json(const nlohmann::json& j, vec4& v);
void to_json(nlohmann::json& j, const vec4& v);
}  // namespace glm

namespace Luna {
class AABB;

void from_json(const nlohmann::json& j, AABB& aabb);
void to_json(nlohmann::json& j, const AABB& aabb);
}  // namespace Luna
