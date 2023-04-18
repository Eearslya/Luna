#include <Luna/Editor/MeshGltfImporter.hpp>
#include <Luna/Editor/MeshImportWindow.hpp>
#include <Luna/UI/UI.hpp>

namespace Luna {
MeshImportWindow::MeshImportWindow(const Path& meshPath) : _meshPath(meshPath) {}

bool MeshImportWindow::Closed() {
	return _closed;
}

void MeshImportWindow::OnProjectChanged() {
	_closed = true;
}

void MeshImportWindow::Update(double deltaTime) {
	if (!_opened) {
		ImGui::OpenPopup("Mesh Import Wizard##MeshImportWindow");
		_opened = true;
	}

	bool open = true;
	if (ImGui::BeginPopupModal("Mesh Import Wizard##MeshImportWindow", &open)) {
		bool close = false;

		ImGui::Text("Import File: %s", _meshPath.Filename().c_str());

		ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_TitleBgActive));
		if (ImGui::Button("Import")) {
			bool imported = false;

			const auto extension = _meshPath.Extension();
			if (extension == "gltf" || extension == "glb") { imported = MeshGltfImporter::Import(_meshPath); }

			close = true;
		}
		ImGui::PopStyleColor();
		ImGui::SameLine();
		if (ImGui::Button("Cancel")) { close = true; }

		if (close) {
			ImGui::CloseCurrentPopup();
			_closed = true;
		}

		ImGui::EndPopup();
	}

	if (!open) { _closed = true; }
}
}  // namespace Luna
