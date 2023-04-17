#include <Luna/UI/UI.hpp>

namespace Luna {
namespace UI {
void BeginDockspace(bool menuBar) {
	ImGuiDockNodeFlags dockspaceFlags = ImGuiDockNodeFlags_None;
	ImGuiWindowFlags windowFlags      = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
	                               ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
	                               ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
	if (menuBar) { windowFlags |= ImGuiWindowFlags_MenuBar; }

	ImGuiViewport* viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(viewport->Pos);
	ImGui::SetNextWindowSize(viewport->Size);
	ImGui::SetNextWindowViewport(viewport->ID);

	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::Begin("Dockspace", nullptr, windowFlags);
	ImGui::PopStyleVar(3);

	ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(370.0f, 64.0f));
	ImGuiID dockspaceId = ImGui::GetID("Dockspace");
	ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), dockspaceFlags);
	ImGui::PopStyleVar();
}

void EndDockspace() {
	ImGui::End();
}
}  // namespace UI
}  // namespace Luna
