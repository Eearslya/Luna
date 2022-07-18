#include "ContentBrowserPanel.hpp"

#include <imgui.h>
#include <imgui_internal.h>

void ContentBrowserPanel::Render(bool* show) {
	if (ImGui::Begin("Content Browser", show, ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoScrollbar)) {
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 8.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 4.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(10.0f, 2.0f));

		ImGuiTableFlags tableFlags =
			ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersInnerV;
		if (ImGui::BeginTable("ContentBrowser", 2, tableFlags, ImVec2(0.0f, 0.0f))) {
			ImGui::TableSetupColumn("Outliner", 0, 300.0f);
			ImGui::TableSetupColumn("Directory Structure", ImGuiTableColumnFlags_WidthStretch);

			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);

			ImGui::BeginChild("##Outliner");
			{
				if (ImGui::CollapsingHeader("Content", nullptr, ImGuiTreeNodeFlags_DefaultOpen)) {
					ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
					ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32_DISABLE);
					ImGui::PushStyleColor(ImGuiCol_HeaderActive, IM_COL32_DISABLE);

					ImGui::PopStyleColor(2);
					ImGui::PopStyleVar();
				}
			}
			ImGui::EndChild();

			ImGui::TableSetColumnIndex(1);

			const float topBarHeight    = 26.0f;
			const float bottomBarHeight = 32.0f;
			ImGui::BeginChild(
				"##DirectoryStructure",
				ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetWindowHeight() - topBarHeight - bottomBarHeight));
			{}
			ImGui::EndChild();

			ImGui::EndTable();
		}

		ImGui::PopStyleVar(3);
	}
	ImGui::End();
}
