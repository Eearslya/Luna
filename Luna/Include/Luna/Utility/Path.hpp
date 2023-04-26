#pragma once

#include <filesystem>
#include <string>

namespace Luna {
class Path {
 public:
	Path();
	Path(const char* pathStr);
	Path(const std::string& pathStr);
	Path(const std::filesystem::path& fsPath);

	Path operator/(const Path& other) const {
		return Join(other);
	}
	Path& operator/=(const Path& other) {
		*this = Join(other);

		return *this;
	}

	operator std::string() const {
		return _pathStr;
	}
	bool operator==(const Path& other) const {
		return _pathStr == other._pathStr;
	}
	bool operator!=(const Path& other) const {
		return _pathStr != other._pathStr;
	}

	bool IsAbsolute() const;
	bool IsRoot() const;

	Path BaseDirectory() const;
	std::string Extension() const;
	std::string Filename() const;
	Path Join(const Path& path) const;
	std::string Protocol() const;
	std::pair<std::string, std::string> ProtocolSplit() const;
	Path Relative(const Path& other) const;
	std::string Stem() const;
	std::string WithoutProtocol() const;

	const std::string& String() const;
	std::wstring WString() const;

 private:
	std::string _pathStr;
};
}  // namespace Luna

namespace std {
template <>
struct hash<Luna::Path> {
	size_t operator()(const Luna::Path& path) const {
		return std::hash<std::string>{}(path.String());
	}
};
}  // namespace std
