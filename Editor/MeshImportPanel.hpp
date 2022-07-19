#pragma once

#include <Vulkan/Common.hpp>
#include <filesystem>

class MeshImportPanel {
 public:
	MeshImportPanel(Luna::Vulkan::WSI& wsi, const std::filesystem::path& meshAssetFile);
	~MeshImportPanel() noexcept;

	bool Render(Luna::Vulkan::CommandBufferHandle& cmd);

 private:
	Luna::Vulkan::WSI& _wsi;
	std::filesystem::path _meshAssetFile;

	bool _open = false;
};
