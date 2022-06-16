#pragma once

#include <Luna/Assets/Asset.hpp>
#include <filesystem>
#include <string>

namespace Luna {
class IAssetImporter {
 public:
	virtual ~IAssetImporter() = default;

	virtual bool CanImport(const std::filesystem::path& assetPath) const                          = 0;
	virtual std::filesystem::path Import(const std::filesystem::path& assetPath,
	                                     const std::filesystem::path& targetAssetDirectory) const = 0;
};
}  // namespace Luna
