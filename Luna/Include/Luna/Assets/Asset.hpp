#pragma once

#include <Luna/Assets/AssetType.hpp>
#include <Luna/Utility/IntrusivePtr.hpp>
#include <Luna/Utility/UUID.hpp>

namespace Luna {
using AssetHandle = UUID;

class Asset : public IntrusivePtrEnabled<Asset> {
 public:
	AssetHandle Handle = {};

	virtual ~Asset() = default;

	virtual bool operator==(const Asset& other) const {
		return Handle == other.Handle;
	}
	virtual bool operator!=(const Asset& other) const {
		return Handle != other.Handle;
	}
};
}  // namespace Luna
