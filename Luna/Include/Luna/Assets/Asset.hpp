#pragma once

#include <Luna/Assets/AssetType.hpp>
#include <Luna/Utility/Ref.hpp>
#include <Luna/Utility/UUID.hpp>

namespace Luna {
using AssetHandle = UUID;

class Asset : public RefCounted {
 public:
	virtual AssetType GetAssetType() const {
		return AssetType::None;
	}

	virtual bool operator==(const Asset& other) const {
		return Handle == other.Handle;
	}
	virtual bool operator!=(const Asset& other) const {
		return Handle != other.Handle;
	}

	AssetHandle Handle;
};
}  // namespace Luna
