#pragma once

#include <filesystem>
#include <iostream>
#include <vector>

namespace Luna {
enum class SpecialFolder { Documents, ApplicationData };

class Filesystem {
 public:
	Filesystem();
	~Filesystem() noexcept;

	static Filesystem* Get() {
		return _instance;
	}

	std::filesystem::path GetSpecialFolder(SpecialFolder folder) const;

	void AddSearchPath(const std::string& path);
	void ClearSearchPaths();
	bool Exists(const std::filesystem::path& path);
	std::vector<std::string> Files(const std::filesystem::path& path, bool recursive = true);
	void RemoveSearchPath(const std::string& path);

	std::optional<std::string> Read(const std::filesystem::path& path);
	std::optional<std::vector<uint8_t>> ReadBytes(const std::filesystem::path& path);

 private:
	static Filesystem* _instance;

	std::filesystem::path FindPath(const std::filesystem::path& path) const;

	std::vector<std::string> _searchPaths;
};
}  // namespace Luna
