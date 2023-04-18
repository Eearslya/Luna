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
		ImGui::Text("Import File: %s", _meshPath.Filename().c_str());

		ImGui::EndPopup();
	}

	if (!open) { _closed = true; }
}
}  // namespace Luna
