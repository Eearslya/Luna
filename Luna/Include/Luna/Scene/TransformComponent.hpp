#pragma once

#include <imgui.h>

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/euler_angles.hpp>

namespace Luna {
struct TransformComponent {
	entt::entity Parent = {};
	std::vector<entt::entity> Children;
	std::string Name   = "Entity";
	glm::vec3 Position = glm::vec3(0, 0, 0);
	glm::vec3 Rotation = glm::vec3(0, 0, 0);
	glm::vec3 Scale    = glm::vec3(1, 1, 1);

	mutable glm::mat4 CachedGlobalTransform = glm::mat4(1.0f);

	void UpdateGlobalTransform(const entt::registry& registry) const {
		glm::mat4 myTransform           = glm::translate(glm::mat4(1.0f), Position);
		const glm::vec3 rotationRadians = glm::radians(Rotation);
		myTransform *= glm::mat4(glm::eulerAngleXYZ(rotationRadians.x, rotationRadians.y, rotationRadians.z));
		myTransform = glm::scale(myTransform, Scale);

		if (registry.valid(Parent)) {
			const auto& parentTransform = registry.get<TransformComponent>(Parent);
			CachedGlobalTransform       = parentTransform.CachedGlobalTransform * myTransform;
		} else {
			CachedGlobalTransform = myTransform;
		}

		for (const auto& child : Children) {
			if (registry.valid(child)) {
				const auto& childTransform = registry.get<TransformComponent>(child);
				childTransform.UpdateGlobalTransform(registry);
			}
		}
	}

	void DrawComponent(const entt::registry& registry) {
		if (ImGui::CollapsingHeader("Transform##TransformComponent", ImGuiTreeNodeFlags_DefaultOpen)) {
			char nameBuf[64];
			strncpy_s(nameBuf, Name.c_str(), sizeof(nameBuf));
			if (ImGui::InputText(
						"Name##TransformComponent", nameBuf, sizeof(nameBuf), ImGuiInputTextFlags_EnterReturnsTrue)) {
				if (strlen(nameBuf) > 0) { Name = nameBuf; }
			}

			bool transformDirty = false;
			if (ImGui::DragFloat3("Position##TransformComponent", glm::value_ptr(Position), 0.1f)) { transformDirty = true; }
			if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
				Position       = glm::vec3(0, 0, 0);
				transformDirty = true;
			}
			if (ImGui::DragFloat3("Rotation##TransformComponent", glm::value_ptr(Rotation), 1.0f)) { transformDirty = true; }
			if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
				Rotation       = glm::vec3(0, 0, 0);
				transformDirty = true;
			}
			if (ImGui::DragFloat3("Scale##TransformComponent", glm::value_ptr(Scale), 0.1f, 0.0f)) { transformDirty = true; }
			if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
				Scale          = glm::vec3(1, 1, 1);
				transformDirty = true;
			}

			if (transformDirty) { UpdateGlobalTransform(registry); }
		}
	}
};
}  // namespace Luna
