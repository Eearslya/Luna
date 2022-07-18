#pragma once

#include <filesystem>
#include <memory>
#include <vector>

#include "Assets/Mesh.hpp"
#include "Vulkan/Common.hpp"

class AssetManager {
 public:
	static void Initialize(Luna::Vulkan::WSI& wsi);
	static void Shutdown();

	static Mesh* GetMesh(const std::filesystem::path& meshAssetPath);

 private:
	static Mesh* LoadMesh(const std::filesystem::path& meshAssetPath);

	static Luna::Vulkan::WSI* _wsi;
	static std::unordered_map<std::string, std::unique_ptr<Mesh>> _meshes;
};
