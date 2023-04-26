#include <imgui_internal.h>

#include <Luna/Assets/AssetManager.hpp>
#include <Luna/Editor/ContentBrowserWindow.hpp>
#include <Luna/Editor/Editor.hpp>
#include <Luna/Editor/EditorAssets.hpp>
#include <Luna/Platform/Filesystem.hpp>
#include <Luna/UI/UI.hpp>
#include <Luna/UI/UIManager.hpp>
#include <Luna/Vulkan/Image.hpp>

namespace Luna {
ContentBrowserWindow::ContentBrowserWindow() {}

void ContentBrowserWindow::OnProjectChanged() {
	ChangeDirectory("/Assets");
	_viewSources = false;
}

void ContentBrowserWindow::Update(double deltaTime) {
	if (ImGui::Begin("Content Browser")) {
		const Path assetsDir = "/Assets";
		const Path sourceDir = "/Sources";
		const Path rootDir   = _viewSources ? sourceDir : assetsDir;
		const bool inRootDir = _currentDirectory == rootDir;

		const auto assetsColor = ImGui::GetStyleColorVec4(!_viewSources ? ImGuiCol_TitleBgActive : ImGuiCol_Button);
		const auto sourceColor = ImGui::GetStyleColorVec4(_viewSources ? ImGuiCol_TitleBgActive : ImGuiCol_Button);
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(-1, 0));
		ImGui::PushStyleColor(ImGuiCol_Button, assetsColor);
		if (UI::ButtonExRounded("Assets", ImVec2(0, 0), 0, ImDrawFlags_RoundCornersLeft)) {
			if (_viewSources) { ChangeDirectory(assetsDir); }
			_viewSources = false;
		}
		ImGui::PopStyleColor();
		ImGui::SameLine();
		ImGui::PushStyleColor(ImGuiCol_Button, sourceColor);
		if (UI::ButtonExRounded("Source", ImVec2(0, 0), 0, ImDrawFlags_RoundCornersRight)) {
			if (!_viewSources) { ChangeDirectory(sourceDir); }
			_viewSources = true;
		}
		ImGui::PopStyleColor();
		ImGui::PopStyleVar();

		ImGui::SameLine();
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(-1, 0));
		if (inRootDir) { ImGui::BeginDisabled(); }
		if (UI::ButtonExRounded(ICON_FA_TURN_UP, ImVec2(0, 0), 0, ImDrawFlags_RoundCornersLeft)) {
			ChangeDirectory(_currentDirectory.BaseDirectory());
		}
		if (inRootDir) { ImGui::EndDisabled(); }
		ImGui::SameLine();
		if (UI::ButtonExRounded(ICON_FA_ARROW_ROTATE_RIGHT, ImVec2(0, 0), 0, ImDrawFlags_RoundCornersRight)) {
			ChangeDirectory(_currentDirectory);
		}
		ImGui::PopStyleVar();

		const auto& style      = ImGui::GetStyle();
		const float buttonSize = 128.0f;
		const float cellSize   = buttonSize + style.ItemSpacing.x;
		const float panelWidth = ImGui::GetContentRegionAvail().x;
		const int columns      = std::max(int(panelWidth / cellSize), 1);

		const auto renamePopupId = ImGui::GetID("Rename");

		if (ImGui::BeginTable("ContentBrowser_Contents", columns)) {
			ImGuiWindow* window = ImGui::GetCurrentWindow();

			bool refresh = false;
			Path newDirectory;
			const auto RenderItem = [&](const Path& relativePath, bool directory) {
				const auto path = relativePath.Filename();

				const auto& icon = directory ? EditorAssets::DirectoryIcon : EditorAssets::FileIcon;

				ImGui::PushID(path.c_str());
				ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));

				ImGui::TableNextColumn();
				ImGui::BeginGroup();

				ImGui::ImageButton(UIManager::Texture(icon),
				                   ImVec2(buttonSize, buttonSize),
				                   ImVec2(0, 0),
				                   ImVec2(1, 1),
				                   -1,
				                   ImVec4(0, 0, 0, 0),
				                   ImGui::ColorConvertU32ToFloat4(IM_COL32(126, 62, 237, 255)));

				if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
					if (directory) {
						newDirectory = _currentDirectory / path;
					} else {
						Editor::RequestAsset(relativePath);
					}
				}
				if (ImGui::BeginDragDropSource()) {
					_currentDragDropItem.ContentPathType = directory ? PathType::Directory : PathType::File;
					_currentDragDropItem.ContentPath     = _currentDirectory / path;
					ImGui::SetDragDropPayload("EditorContent", &_currentDragDropItem, sizeof(_currentDragDropItem));
					ImGui::Text("%s", relativePath.Filename().c_str());

					ImGui::EndDragDropSource();
				}
				if (!directory && ImGui::BeginPopupContextWindow(nullptr, ImGuiPopupFlags_MouseButtonRight)) {
					if (ImGui::MenuItem(ICON_FA_PEN_TO_SQUARE " Rename")) {
						const auto basePath = _currentDirectory;
						const auto filename = path;
						UIManager::TextDialog(
							"Rename",
							[this, basePath, filename](bool renamed, const std::string& newName) {
								if (!renamed) { return; }

								AssetManager::RenameAsset(AssetManager::GetAssetMetadata(basePath / filename), newName);
								ChangeDirectory(_currentDirectory);
							},
							filename);
					}

					ImGui::EndPopup();
				}

				ImGui::TextWrapped("%s", path.c_str());

				ImGui::EndGroup();

				ImGui::PopStyleColor();
				ImGui::PopID();
			};

			for (const auto& directory : _directories) { RenderItem(directory, true); }
			for (const auto& file : _files) { RenderItem(file, false); }

			if (!newDirectory.String().empty()) { ChangeDirectory(newDirectory); }
			if (refresh) { ChangeDirectory(_currentDirectory); }

			ImGui::EndTable();
		}
	}
	ImGui::End();
}

void ContentBrowserWindow::ChangeDirectory(const Path& directory) {
	_currentDirectory = directory;

	_directories.clear();
	_files.clear();

	auto* backend = Filesystem::GetBackend("project");
	if (!backend) { return; }

	for (auto entry : backend->List(_currentDirectory)) {
		if (entry.Type == PathType::Directory) {
			_directories.push_back(entry.Path);
		} else {
			_files.push_back(entry.Path);
		}
	}

	std::sort(
		_directories.begin(), _directories.end(), [](const Path& a, const Path& b) { return a.String() < b.String(); });
	std::sort(_files.begin(), _files.end(), [](const Path& a, const Path& b) { return a.String() < b.String(); });
}
}  // namespace Luna
