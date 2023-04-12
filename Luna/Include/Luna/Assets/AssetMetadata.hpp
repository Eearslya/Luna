#pragma once

#include <Luna/Assets/Asset.hpp>
#include <Luna/Utility/Path.hpp>

namespace Luna {
struct AssetMetadata {
	Path FilePath;
	AssetHandle Handle = 0;
	AssetType Type;

	bool IsValid() const {
		return uint64_t(Handle) != 0;
	}
};
}  // namespace Luna
