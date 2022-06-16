#pragma once

#include <string>

namespace Luna {
enum class AssetType : uint16_t { None = 0, Scene = 1, Mesh = 2, StaticMesh = 3, Material = 4, Texture = 5 };

static inline AssetType AssetTypeFromExtension(const std::string& ext) {
	if (ext.compare(".lscene") == 0) { return AssetType::Scene; }
	if (ext.compare(".lmesh") == 0) { return AssetType::Mesh; }
	if (ext.compare(".lsmesh") == 0) { return AssetType::StaticMesh; }
	if (ext.compare(".lmaterial") == 0) { return AssetType::Material; }
	if (ext.compare(".ltexture") == 0) { return AssetType::Texture; }

	return AssetType::None;
}

static inline AssetType AssetTypeFromString(const std::string& type) {
	if (type.compare("None") == 0) { return AssetType::None; }
	if (type.compare("Scene") == 0) { return AssetType::Scene; }
	if (type.compare("Mesh") == 0) { return AssetType::Mesh; }
	if (type.compare("StaticMesh") == 0) { return AssetType::StaticMesh; }
	if (type.compare("Material") == 0) { return AssetType::Material; }
	if (type.compare("Texture") == 0) { return AssetType::Texture; }

	return AssetType::None;
}

static inline const char* AssetTypeToString(AssetType type) {
	switch (type) {
		case AssetType::None:
			return "None";
		case AssetType::Scene:
			return "Scene";
		case AssetType::Mesh:
			return "Mesh";
		case AssetType::StaticMesh:
			return "StaticMesh";
		case AssetType::Material:
			return "Material";
		case AssetType::Texture:
			return "Texture";
	}

	return "None";
}
}  // namespace Luna
