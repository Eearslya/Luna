#pragma once

#include <Luna/Assets/Asset.hpp>
#include <Luna/Assets/AssetRegistry.hpp>
#include <atomic>

namespace Luna {
class AssetManager final {
 public:
	static void Initialize();
	static void Shutdown();

	static void EnqueueAssetLoad(const AssetMetadata& metadata);
	static const AssetMetadata& GetAssetMetadata(const Path& assetPath);
	static const AssetMetadata& GetAssetMetadata(AssetHandle handle);
	static Path GetFilesystemPath(const AssetMetadata& metadata);
	static AssetHandle ImportAsset(const Path& assetPath);
	static bool LoadAsset(const AssetMetadata& metadata, IntrusivePtr<Asset>& asset);
	static void RenameAsset(const AssetMetadata& metadata, const std::string& newName);
	static void SaveAsset(const AssetMetadata& metadata, const IntrusivePtr<Asset>& asset);
	static void SaveLoaded();
	static void UnloadAsset(const AssetMetadata& metadata);

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

	template <typename T>
	static IntrusivePtr<T> GetAsset(AssetHandle handle, bool async = false) {
		auto& metadata = GetAssetMetadata(handle);
		if (!metadata.IsValid()) { return {}; }

		IntrusivePtr<Asset> asset = {};
		if (LoadedAssets.find(metadata.Handle) == LoadedAssets.end()) {
			if (async) {
				EnqueueAssetLoad(metadata);
			} else {
				if (LoadAsset(metadata, asset)) { LoadedAssets[metadata.Handle] = asset; }
			}
		} else {
			asset = LoadedAssets[metadata.Handle];
		}

		return IntrusivePtr<T>(std::move(asset));
	}

	static AssetRegistry& GetRegistry();

 private:
	static std::unordered_map<AssetHandle, IntrusivePtr<Asset>> LoadedAssets;
	static AssetRegistry Registry;
	static std::mutex AsyncLock;
	static std::vector<AssetHandle> AsyncRequests;

	static void LoadAssets();
	static void LoadRegistry();
	static void SaveRegistry();
};
}  // namespace Luna
