#pragma once

#include <imgui.h>

#include <Luna/Assets/Material.hpp>
#include <Luna/Assets/StaticMesh.hpp>
#include <Luna/Assets/Texture.hpp>
#include <glm/glm.hpp>

namespace Luna {
struct MeshRenderer {
	StaticMeshHandle Mesh;
	std::vector<MaterialHandle> Materials;

	void DrawComponent(entt::registry& registry) {
		if (ImGui::CollapsingHeader("MeshRenderer##MeshRenderer", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::Text("Mesh: %s", Mesh->Ready ? "Ready" : "Loading");
		}
	}
};
}  // namespace Luna
