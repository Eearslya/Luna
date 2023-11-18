#pragma once

#include <Luna/Common.hpp>

namespace Luna {
class PathIterator;

class Path {
	friend class PathIterator;

 public:
	Path();
	Path(const char* pathStr);
	Path(const std::string& pathStr);
	Path(const std::string_view pathStr);
	Path(const std::filesystem::path& fsPath);

	[[nodiscard]] bool Empty() const;
	[[nodiscard]] bool IsAbsolute() const;
	[[nodiscard]] bool IsRelative() const;
	[[nodiscard]] bool IsRoot() const;
	[[nodiscard]] bool ValidateBounds() const;

	[[nodiscard]] Path Normalized() const;
	[[nodiscard]] const std::string& String() const;

	[[nodiscard]] std::string_view Extension() const;
	[[nodiscard]] std::string_view Filename() const;
	[[nodiscard]] std::string_view FilePath() const;
	[[nodiscard]] std::string_view ParentPath() const;
	[[nodiscard]] std::string_view Protocol() const;
	[[nodiscard]] std::string_view Stem() const;

	[[nodiscard]] Path Relative(const Path& other) const;

	[[nodiscard]] operator std::string() const;
	[[nodiscard]] bool operator==(const Path& other) const;
	[[nodiscard]] bool operator!=(const Path& other) const;

	[[nodiscard]] Path operator/(const char* path) const;
	[[nodiscard]] Path operator/(std::string_view path) const;
	[[nodiscard]] Path operator/(const std::string& other) const;
	[[nodiscard]] Path operator/(const Path& other) const;
	Path& operator/=(const char* path);
	Path& operator/=(std::string_view path);
	Path& operator/=(const std::string& other);
	Path& operator/=(const Path& other);

	PathIterator begin() const;
	PathIterator end() const noexcept;

 private:
	std::string _pathStr;
};

class PathIterator {
	using BaseIteratorT = std::string::const_iterator;

 public:
	PathIterator();
	PathIterator(const BaseIteratorT& position, const Path* parent);
	PathIterator(const BaseIteratorT& position, std::string_view element, const Path* parent);
	PathIterator(const PathIterator&)            = default;
	PathIterator(PathIterator&&)                 = default;
	PathIterator& operator=(const PathIterator&) = default;
	PathIterator& operator=(PathIterator&&)      = default;

	PathIterator& operator++();
	[[nodiscard]] const std::string_view* operator->() const noexcept;
	[[nodiscard]] const std::string_view& operator*() const noexcept;
	[[nodiscard]] bool operator==(const PathIterator& other) const;
	[[nodiscard]] bool operator!=(const PathIterator& other) const;

 private:
	std::string::const_iterator _position = {};
	std::string_view _element             = {};
	const Path* _parent                   = nullptr;
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

template <>
struct std::formatter<Luna::Path> : std::formatter<std::string> {
	auto format(const Luna::Path& path, format_context& ctx) const -> decltype(ctx.out()) {
		return format_to(ctx.out(), "{}", std::string(path));
	}
};
