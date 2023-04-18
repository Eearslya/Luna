#pragma once

#include <Luna/Assets/AssetType.hpp>
#include <Luna/Utility/Path.hpp>
#include <string>
#include <vector>

namespace Luna {
class AssetFile {
 public:
	bool Load(const Path& path);
	bool Save(const Path& path);

	uint32_t FileVersion = 1;

	std::vector<uint8_t> Binary;
	std::string Json;
	AssetType Type;
};
}  // namespace Luna
