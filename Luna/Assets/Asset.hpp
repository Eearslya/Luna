#pragma once

#include "AssetTypes.hpp"
#include "Utility/IntrusivePtr.hpp"
#include "Utility/UUID.hpp"

namespace Luna {
using AssetHandle = UUID;

class Asset : public IntrusivePtrEnabled<Asset> {
 public:
	virtual ~Asset() = default;

	virtual AssetType GetAssetType() const {
		return AssetType::None;
	}
	bool IsValid() const {
		return !(Flags & (AssetFlagBits::Missing | AssetFlagBits::Invalid));
	}

	bool operator==(const Asset& other) const {
		return Handle == other.Handle;
	}
	bool operator!=(const Asset& other) const {
		return Handle != other.Handle;
	}

	AssetFlags Flags   = {};
	AssetHandle Handle = 0;
};
}  // namespace Luna
