#include <imgui.h>

#include <Luna/Assets/Environment.hpp>
#include <entt/entt.hpp>

namespace Luna {
struct WorldData {
	EnvironmentHandle Environment;

	void DrawComponent(entt::registry& registry) {
		if (ImGui::CollapsingHeader("WorldData##WorldData", ImGuiTreeNodeFlags_DefaultOpen)) { ImGui::Text("World Data"); }
	}
};
}  // namespace Luna
