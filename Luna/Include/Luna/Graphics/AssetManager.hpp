#pragma once

#include <tiny_gltf.h>

#include <Luna/Assets/Material.hpp>
#include <Luna/Assets/StaticMesh.hpp>
#include <Luna/Assets/Texture.hpp>
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
	std::vector<Texture*> Textures;
	std::vector<Material*> Materials;
	std::vector<StaticMesh*> Meshes;
};

class AssetManager {
 public:
	AssetManager();
	~AssetManager() noexcept;

	void LoadModel(const std::string& gltfFile, Scene& scene, const entt::entity parentEntity);

	void FreeMaterial(Material* material);
	void FreeStaticMesh(StaticMesh* mesh);
	void FreeTexture(Texture* texture);

 private:
	void LoadGltfTask(const std::string& gltfFile, Scene& scene, const entt::entity parentEntity);
	void LoadMeshTask(ModelLoadContext* context, size_t meshIndex) const;
	void LoadMaterialsTask(ModelLoadContext* context) const;
	void LoadTextureTask(ModelLoadContext* context, size_t textureIndex) const;

	ObjectPool<ModelLoadContext> _contextPool;
	ObjectPool<Material> _materialPool;
	ObjectPool<StaticMesh> _staticMeshPool;
	ObjectPool<Texture> _texturePool;
};
}  // namespace Luna
