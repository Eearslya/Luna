#pragma once

#include <Luna/Assets/Asset.hpp>

namespace Luna {
class Mesh : public Asset {
 public:
	static AssetType GetAssetType() {
		return AssetType::Mesh;
	}
};
}  // namespace Luna
