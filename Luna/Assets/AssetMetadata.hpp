#pragma once

#include <filesystem>

#include "Asset.hpp"

namespace Luna {
struct AssetMetadata {
	AssetHandle Handle = 0;
	AssetType Type     = AssetType::None;

	std::filesystem::path FilePath;
	bool Loaded = false;
	bool Memory = false;

	bool IsValid() const {
		return Handle != 0 && !Memory;
	}
};
}  // namespace Luna
