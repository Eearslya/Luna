#pragma once

#include <Luna/Assets/Asset.hpp>
#include <Luna/Assets/AssetType.hpp>
#include <filesystem>

namespace Luna {
struct AssetMetadata {
	AssetHandle Handle;
	AssetType Type = AssetType::None;
	std::filesystem::path Path;
	std::filesystem::path Source;
	bool Loaded = false;
};
}  // namespace Luna
