#pragma once

#include <Luna/Utility/Ref.hpp>
#include <filesystem>
#include <string>

namespace Luna {
class Project : public RefCounted {
 public:
	Project(const std::filesystem::path& projectPath, bool create = false);
	~Project() noexcept;

	const std::filesystem::path& GetAssetDirectoryPath() const {
		return _assetDirectoryPath;
	}
	const std::filesystem::path& GetAssetRegistryPath() const {
		return _assetRegistryPath;
	}
	const std::filesystem::path& GetProjectPath() const {
		return _projectPath;
	}

	void Load();
	void Save() const;

	std::string Name              = "NewProject";
	std::string AssetDirectory    = "Assets";
	std::string AssetRegistryPath = "AssetRegistry.lregistry";

	static std::filesystem::path GetProjectsFolder();

 private:
	void Create();

	std::filesystem::path _assetDirectoryPath;
	std::filesystem::path _assetRegistryPath;
	std::filesystem::path _projectPath;
	std::filesystem::path _projectFilePath;
};
}  // namespace Luna
