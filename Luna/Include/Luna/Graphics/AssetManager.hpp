#pragma once

#include <tiny_gltf.h>

#include <Luna/Assets/Environment.hpp>
#include <Luna/Assets/Material.hpp>
#include <Luna/Assets/StaticMesh.hpp>
#include <Luna/Assets/Texture.hpp>
#include <Luna/Time/Time.hpp>
#include <Luna/Utility/IntrusivePtr.hpp>
#include <Luna/Utility/ObjectPool.hpp>
#include <entt/entt.hpp>

namespace Luna {
class Scene;

struct ModelLoadContext {
	ElapsedTime LoadTime;
	std::string File;
	std::string FileName;
	std::string FilePath;
	tinygltf::Model Model;
	std::vector<TextureHandle> Textures;
	std::vector<vk::Format> TextureFormats;
	std::vector<MaterialHandle> Materials;
	std::vector<StaticMeshHandle> Meshes;
};

class AssetManager {
 public:
	AssetManager();
	~AssetManager() noexcept;

	void LoadEnvironment(const std::string& filePath, Scene& scene);
	void LoadModel(const std::string& gltfFile, Scene& scene, const entt::entity parentEntity);

	void FreeEnvironment(Environment* environment);
	void FreeMaterial(Material* material);
	void FreeStaticMesh(StaticMesh* mesh);
	void FreeTexture(Texture* texture);

 private:
	void LoadEnvironmentTask(const std::string& filePath, Scene& scene);
	void LoadGltfTask(const std::string& gltfFile, Scene& scene, const entt::entity parentEntity);
	void LoadMeshTask(ModelLoadContext* context, size_t meshIndex) const;
	void LoadMaterialsTask(ModelLoadContext* context) const;
	void LoadTextureTask(ModelLoadContext* context, size_t textureIndex) const;

	ThreadSafeObjectPool<Environment> _environmentPool;
	ThreadSafeObjectPool<ModelLoadContext> _contextPool;
	ThreadSafeObjectPool<Material> _materialPool;
	ThreadSafeObjectPool<StaticMesh> _staticMeshPool;
	ThreadSafeObjectPool<Texture> _texturePool;
};
}  // namespace Luna
