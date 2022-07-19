#include "ContentBrowserPanel.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include "Editor.hpp"
#include "UI.hpp"

using namespace Luna;

ContentBrowserPanel::ContentBrowserPanel() : _currentDirectory(Editor::AssetsDirectory) {}

void ContentBrowserPanel::Render(bool* show) {
	if (ImGui::Begin("Content Browser", show)) {
		if (_currentDirectory != Editor::AssetsDirectory) {
			if (ImGui::Button(ICON_FA_ARROW_LEFT_LONG)) { _currentDirectory = _currentDirectory.parent_path(); }
		}

		const auto& style      = ImGui::GetStyle();
		const float buttonSize = 128.0f;
		const float cellSize   = buttonSize + style.ItemSpacing.x;
		const float panelWidth = ImGui::GetContentRegionAvail().x;
		const int columns      = std::max(int(panelWidth / cellSize), 1);

		if (ImGui::BeginTable("ContentBrowser_Contents", columns)) {
			ImGuiWindow* window = ImGui::GetCurrentWindow();

			std::vector<std::filesystem::path> directories;
			std::vector<std::filesystem::path> files;
			for (auto entry : std::filesystem::directory_iterator(_currentDirectory)) {
				const auto relativePath = std::filesystem::relative(entry.path(), Editor::AssetsDirectory);
				const bool directory    = entry.is_directory();
				if (directory) {
					directories.push_back(relativePath);
				} else {
					files.push_back(relativePath);
				}
			}

			std::sort(
				directories.begin(), directories.end(), [](const auto& a, const auto& b) { return a.string() < b.string(); });
			std::sort(files.begin(), files.end(), [](const auto& a, const auto& b) { return a.string() < b.string(); });

			const auto RenderItem = [&](const std::filesystem::path& relativePath, bool directory) {
				const auto path    = relativePath.filename();
				const auto pathStr = path.string();

				auto& icon = directory ? Editor::Get()->GetResources().DirectoryIcon : Editor::Get()->GetResources().FileIcon;

				ImGui::PushID(pathStr.c_str());
				ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));

				ImGui::TableNextColumn();
				ImGui::BeginGroup();

				ImGui::ImageButton(UI::TextureID(icon), ImVec2(buttonSize, buttonSize));
				if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
					if (directory) {
						_currentDirectory /= path;
					} else {
						ContentBrowserItem item{.Type = ContentBrowserItemType::File, .FilePath = relativePath};
						Editor::Get()->RequestContent(item);
					}
				}
				if (ImGui::BeginDragDropSource()) {
					_currentDragDropItem.Type     = directory ? ContentBrowserItemType::Directory : ContentBrowserItemType::File;
					_currentDragDropItem.FilePath = relativePath;
					ImGui::SetDragDropPayload("ContentBrowserItem", &_currentDragDropItem, sizeof(_currentDragDropItem));
					ImGui::Text("%s", pathStr.c_str());

					ImGui::EndDragDropSource();
				}
				ImGui::TextWrapped("%s", pathStr.c_str());

				ImGui::EndGroup();

				ImGui::PopStyleColor();
				ImGui::PopID();
			};

			for (const auto& directory : directories) { RenderItem(directory, true); }
			for (const auto& file : files) { RenderItem(file, false); }

			ImGui::EndTable();
		}
	}
	ImGui::End();
}
