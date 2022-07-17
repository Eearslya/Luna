#pragma once

#include "Utility/EnumClass.hpp"

namespace Luna {
enum class AssetFlagBits { Missing = 1 << 0, Invalid = 1 << 1 };
using AssetFlags = Bitmask<AssetFlagBits>;

enum class AssetType { None = 0, Scene = 1, StaticMesh = 2, Texture = 3 };
}  // namespace Luna

template <>
struct Luna::EnableBitmaskOperators<Luna::AssetFlagBits> : std::true_type {};
