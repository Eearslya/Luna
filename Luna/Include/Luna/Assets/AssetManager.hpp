#pragma once

#include <Luna/Assets/Asset.hpp>
#include <Luna/Assets/AssetRegistry.hpp>

namespace Luna {
class AssetManager final {
 public:
	static void Initialize();
	static void Shutdown();

	static const AssetMetadata& GetAssetMetadata(const Path& assetPath);
	static const AssetMetadata& GetAssetMetadata(AssetHandle handle);
	static Path GetFilesystemPath(const AssetMetadata& metadata);
	static AssetHandle ImportAsset(const Path& assetPath);
	static bool LoadAsset(const AssetMetadata& metadata, IntrusivePtr<Asset>& asset);
	static void SaveAsset(const AssetMetadata& metadata, const IntrusivePtr<Asset>& asset);

	template <typename T, typename... Args>
	static IntrusivePtr<T> CreateAsset(const Path& assetPath, Args&&... args) {
		static_assert(std::is_base_of_v<Asset, T>, "AssetManager can only create Assets!");

		AssetMetadata metadata = {.FilePath = assetPath, .Handle = AssetHandle(), .Type = T::GetAssetType()};

		auto oldMeta = GetAssetMetadata(assetPath);
		if (oldMeta.IsValid()) {
			Registry.Remove(oldMeta.Handle);
			metadata.Handle = oldMeta.Handle;
		}

		Registry[metadata.Handle] = metadata;
		SaveRegistry();

		auto asset    = MakeHandle<T>(std::forward<Args>(args)...);
		asset->Handle = metadata.Handle;
		SaveAsset(metadata, asset);

		return asset;
	}

	static AssetRegistry& GetRegistry();

 private:
	static AssetRegistry Registry;

	static void LoadAssets();
	static void LoadRegistry();
	static void SaveRegistry();
};
}  // namespace Luna
