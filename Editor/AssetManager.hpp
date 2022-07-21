#pragma once

#include <Assets/Mesh.hpp>
#include <Utility/IntrusivePtr.hpp>
#include <Utility/ObjectPool.hpp>
#include <Vulkan/Common.hpp>
#include <filesystem>
#include <memory>
#include <unordered_map>
#include <vector>

class AssetManager {
 public:
	static void Initialize(Luna::Vulkan::WSI& wsi);
	static void Shutdown();

	static Luna::Mesh* GetMesh(const std::filesystem::path& meshAssetPath);

 private:
	static Luna::Mesh* LoadMesh(const std::filesystem::path& meshAssetPath);

	static Luna::Vulkan::WSI* _wsi;
	static std::unordered_map<std::string, Luna::IntrusivePtr<Luna::Mesh>> _meshes;
	static Luna::ObjectPool<Luna::Mesh> _meshPool;
};
