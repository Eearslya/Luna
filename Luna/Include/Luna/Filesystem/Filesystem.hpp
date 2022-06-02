#pragma once

#include <filesystem>
#include <iostream>
#include <vector>

struct PHYSFS_File;

namespace Luna {
enum class FileMode { Read, Write, Append };

class BaseFileStream {
 public:
	explicit BaseFileStream(PHYSFS_File* file);
	virtual ~BaseFileStream() noexcept;

	size_t Length() const;

 protected:
	PHYSFS_File* _file = nullptr;
};

class IFileStream : public BaseFileStream, public std::istream {
 public:
	explicit IFileStream(const std::filesystem::path& filename);
	virtual ~IFileStream() noexcept;
};

class OFileStream : public BaseFileStream, public std::ostream {
 public:
	explicit OFileStream(const std::filesystem::path& filename, FileMode writeMode = FileMode::Write);
	virtual ~OFileStream() noexcept;
};

class FileStream : public BaseFileStream, public std::iostream {
 public:
	explicit FileStream(const std::filesystem::path& filename, FileMode openMode = FileMode::Read);
	virtual ~FileStream() noexcept;
};

class Filesystem {
 public:
	Filesystem();
	~Filesystem() noexcept;

	static Filesystem* Get() {
		return _instance;
	}

	void AddSearchPath(const std::string& path);
	void ClearSearchPaths();
	bool Exists(const std::filesystem::path& path);
	std::vector<std::string> Files(const std::filesystem::path& path, bool recursive = true);
	void RemoveSearchPath(const std::string& path);

	std::optional<std::string> Read(const std::filesystem::path& path);
	std::optional<std::vector<uint8_t>> ReadBytes(const std::filesystem::path& path);

 private:
	static Filesystem* _instance;

	std::vector<std::string> _searchPaths;
};
}  // namespace Luna
