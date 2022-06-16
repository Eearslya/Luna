#pragma once

#include <Luna/Assets/AssetMetadata.hpp>
#include <filesystem>
#include <unordered_map>

namespace std {
template <>
struct hash<std::filesystem::path> {
	std::size_t operator()(const std::filesystem::path& path) const {
		return hash_value(path);
	}
};
}  // namespace std

namespace Luna {
class AssetRegistry {
	using AssetMapT = std::unordered_map<std::filesystem::path, AssetMetadata>;

 public:
	std::filesystem::path GetFilesystemPath(const AssetMetadata& metadata) const;
	std::filesystem::path GetRelativePath(const std::filesystem::path& path) const;
	std::filesystem::path GetPathKey(const std::filesystem::path& path) const;

	std::size_t GetAssetCount() const {
		return _assets.size();
	}

	bool Contains(const std::filesystem::path& path) const;

	void Clear();
	void Load(const std::filesystem::path& registryPath);
	void Remove(const std::filesystem::path& path);
	void Save();

	AssetMetadata& operator[](const std::filesystem::path& path);
	const AssetMetadata& operator[](const std::filesystem::path& path) const;

	AssetMapT::iterator begin() {
		return _assets.begin();
	}
	AssetMapT::iterator end() {
		return _assets.end();
	}
	AssetMapT::const_iterator cbegin() {
		return _assets.cbegin();
	}
	AssetMapT::const_iterator cend() {
		return _assets.cend();
	}

 private:
	AssetMapT _assets;
	std::filesystem::path _loadedRegistryPath;
};
}  // namespace Luna
