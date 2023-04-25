#include <Luna/Assets/AssetManager.hpp>
#include <Luna/Assets/AssetRegistry.hpp>
#include <Luna/Editor/AssetRegistryWindow.hpp>
#include <Luna/UI/UI.hpp>

namespace Luna {
bool AssetRegistryWindow::Closed() {
	return _closed;
}

void AssetRegistryWindow::Update(double deltaTime) {
	bool open = true;
	if (ImGui::Begin("Asset Registry##AssetRegistryWindow", &open)) {
		auto& registry = AssetManager::GetRegistry();

		ImGui::Text("Asset Registry Size: %zu", registry.Size());
		for (const auto& [handle, metadata] : registry) { ImGui::Text("%s", metadata.FilePath.String().c_str()); }
	}
	ImGui::End();

	_closed = !open;
}
}  // namespace Luna
