#include "MeshImportPanel.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include "AssetManager.hpp"
#include "Editor.hpp"
#include "Vulkan/Buffer.hpp"
#include "Vulkan/CommandBuffer.hpp"
#include "Vulkan/Device.hpp"
#include "Vulkan/Image.hpp"
#include "Vulkan/RenderPass.hpp"
#include "Vulkan/WSI.hpp"

using namespace Luna;

MeshImportPanel::MeshImportPanel(Vulkan::WSI& wsi, const std::filesystem::path& meshAssetFile)
		: _wsi(wsi), _meshAssetFile(meshAssetFile) {}

MeshImportPanel::~MeshImportPanel() noexcept {}

bool MeshImportPanel::Render(Luna::Vulkan::CommandBufferHandle& cmd) {
	if (!_open) {
		ImGui::OpenPopup("Model Importer");
		_open = true;
	}
	bool closed = false;

	Mesh* mesh = AssetManager::GetMesh(_meshAssetFile);

	if (ImGui::BeginPopupModal("Model Importer", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		if (ImGui::BeginTable("ModelImporter", 2)) {
			ImGui::TableSetupColumn("Controls", ImGuiTableColumnFlags_WidthFixed, 256.0f);

			ImGui::TableNextColumn();
			ImGui::Text("File");
			ImGui::Separator();
			ImGui::TextWrapped("%s", _meshAssetFile.string().c_str());
			ImGui::Dummy(ImVec2(0.0f, 8.0f));

			ImGui::Text("Name");
			ImGui::Separator();
			ImGui::TextWrapped("%s", _meshAssetFile.filename().string().c_str());
			ImGui::Dummy(ImVec2(0.0f, 8.0f));

			ImGui::Text("Meshes");
			ImGui::Separator();
			ImGui::TextWrapped("1");
			ImGui::Dummy(ImVec2(0.0f, 8.0f));

			ImGui::TableNextColumn();
			ImGui::Dummy(ImVec2(512.0f, 512.0f));

			ImGui::EndTable();
		}

		if (ImGui::Button("Import", ImVec2(120, 0))) { closed = true; }
		ImGui::SameLine();
		if (ImGui::Button("Cancel", ImVec2(120, 0))) { closed = true; }

		ImGui::EndPopup();
	}

	if (closed) { ImGui::CloseCurrentPopup(); }

	return !closed;
}
