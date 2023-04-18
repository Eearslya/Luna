#pragma once

#include <Luna/Utility/Path.hpp>

namespace Luna {
enum class AssetType { None, Material, Mesh, Scene, Texture };

inline AssetType AssetTypeFromPath(const Path& assetPath) {
	const auto ext = assetPath.Extension();

	if (ext == "lmesh") {
		return AssetType::Mesh;
	} else if (ext == "lmaterial") {
		return AssetType::Material;
	} else if (ext == "lscene") {
		return AssetType::Scene;
	} else if (ext == "ltexture") {
		return AssetType::Texture;
	}

	return AssetType::None;
}

inline AssetType AssetTypeFromString(const char* str) {
	if (strcmp(str, "Material") == 0) {
		return AssetType::Material;
	} else if (strcmp(str, "Mesh") == 0) {
		return AssetType::Mesh;
	} else if (strcmp(str, "Scene") == 0) {
		return AssetType::Scene;
	} else if (strcmp(str, "Texture") == 0) {
		return AssetType::Texture;
	}

	return AssetType::None;
}

inline AssetType AssetTypeFromString(const std::string& str) {
	return AssetTypeFromString(str.c_str());
}

inline const char* AssetTypeToString(AssetType type) {
	switch (type) {
		case AssetType::Material:
			return "Material";
		case AssetType::Mesh:
			return "Mesh";
		case AssetType::Scene:
			return "Scene";
		case AssetType::Texture:
			return "Texture";
		default:
			return "None";
	}
}
}  // namespace Luna
