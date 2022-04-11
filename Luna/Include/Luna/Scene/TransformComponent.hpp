#pragma once

#include <imgui.h>

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/euler_angles.hpp>

namespace Luna {
struct TransformComponent {
	bool Enabled        = true;
	entt::entity Parent = entt::null;
	std::vector<entt::entity> Children;
	std::string Name   = "Entity";
	glm::vec3 Position = glm::vec3(0, 0, 0);
	glm::quat Rotation = glm::quat(1, 0, 0, 0);
	glm::vec3 Scale    = glm::vec3(1, 1, 1);

	mutable glm::mat4 CachedGlobalTransform = glm::mat4(1.0f);
	mutable glm::quat CachedGlobalRotation  = glm::quat(1, 0, 0, 0);

	glm::mat4 GetLocalTransform() const {
		glm::mat4 myTransform = glm::translate(glm::mat4(1.0f), Position);
		myTransform *= glm::mat4(Rotation);
		myTransform = glm::scale(myTransform, Scale);

		return myTransform;
	}

	bool IsEnabled(const entt::registry& registry) const {
		if (!Enabled) { return false; }

		if (registry.valid(Parent)) {
			const auto& parentTransform = registry.get<TransformComponent>(Parent);
			return parentTransform.IsEnabled(registry);
		}

		return true;
	}

	void UpdateGlobalTransform(const entt::registry& registry) const {
		const glm::mat4 myTransform = GetLocalTransform();

		if (registry.valid(Parent)) {
			const auto& parentTransform = registry.get<TransformComponent>(Parent);
			CachedGlobalTransform       = parentTransform.CachedGlobalTransform * myTransform;
			CachedGlobalRotation        = parentTransform.CachedGlobalRotation * Rotation;
		} else {
			CachedGlobalTransform = myTransform;
			CachedGlobalRotation  = Rotation;
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
			ImGui::Checkbox("Enabled", &Enabled);

			char nameBuf[64];
#ifdef _WIN32
			strncpy_s(nameBuf, Name.c_str(), sizeof(nameBuf));
#else
			strncpy(nameBuf, Name.c_str(), sizeof(nameBuf));
#endif
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
			glm::vec3 eulerAngles = glm::degrees(glm::eulerAngles(Rotation));
			if (ImGui::DragFloat3("Rotation##TransformComponent", glm::value_ptr(eulerAngles), 1.0f)) {
				transformDirty = true;
			}
			if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
				Rotation       = glm::quat(1, 0, 0, 0);
				transformDirty = true;
			}
			if (ImGui::DragFloat3("Scale##TransformComponent", glm::value_ptr(Scale), 0.1f, 0.01f)) { transformDirty = true; }
			if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
				Scale          = glm::vec3(1, 1, 1);
				transformDirty = true;
			}

			if (transformDirty) {
				const glm::quat newRot = glm::quat(glm::radians(eulerAngles));
				Rotation               = newRot;
				UpdateGlobalTransform(registry);
			}
		}
	}
};
}  // namespace Luna
