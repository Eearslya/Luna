#pragma once

#include <imgui.h>

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace Luna {
struct Light {
	glm::vec3 Color = glm::vec3(1.0f, 1.0f, 1.0f);

	void DrawComponent(entt::registry& registry) {
		if (ImGui::CollapsingHeader("Light##Light", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::ColorEdit3("Color", glm::value_ptr(Color), ImGuiColorEditFlags_HDR);
		}
	}
};
}  // namespace Luna
