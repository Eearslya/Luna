#pragma once

#include <filesystem>

#include "Vulkan/Common.hpp"

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
