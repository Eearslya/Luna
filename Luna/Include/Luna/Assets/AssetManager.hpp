#pragma once

#include <Luna/Assets/Asset.hpp>
#include <Luna/Assets/AssetRegistry.hpp>

namespace Luna {
class AssetManager final {
 public:
	static void Initialize();
	static void Shutdown();

	static const AssetMetadata& GetAssetMetadata(const Path& assetPath);
	static Path GetFilesystemPath(const AssetMetadata& metadata);
	static AssetHandle ImportAsset(const Path& assetPath);

 private:
	static AssetRegistry Registry;

	static void LoadAssets();
	static void LoadRegistry();
	static void SaveRegistry();
};
}  // namespace Luna
