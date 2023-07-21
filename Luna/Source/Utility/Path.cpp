#include <Luna/Utility/Path.hpp>

namespace Luna {
Path::Path() = default;

Path::Path(const char* pathStr) : Path(std::string(pathStr)) {}

Path::Path(const std::string& pathStr) : _pathStr(pathStr) {
	for (auto& c : _pathStr) {
		if (c == '\\') { c = '/'; }
	}
}

Path::Path(std::string_view pathStr) : Path(std::string(pathStr)) {}

Path::Path(const std::filesystem::path& fsPath) : Path(fsPath.string()) {}

bool Path::Empty() const {
	return _pathStr.empty();
}

bool Path::IsAbsolute() const {
	if (_pathStr.empty()) { return false; }
	if (_pathStr.front() == '/') { return true; }

	return _pathStr.find("://") != std::string::npos;
}

bool Path::IsRelative() const {
	return !IsAbsolute();
}

bool Path::IsRoot() const {
	if (_pathStr.empty()) { return false; }
	if (_pathStr.front() == '/' && _pathStr.size() == 1) { return true; }

	const auto index = _pathStr.find("://");

	return index != std::string::npos && (index + 3) == _pathStr.size();
}

Path Path::Normalized() const {
	std::vector<std::string> parts;

	for (const auto& part : *this) {
		if (part == "..") {
			if (!parts.empty()) { parts.pop_back(); }
		} else if (part != ".") {
			parts.push_back(std::string(part));
		}
	}

	const auto newPathStr = fmt::format("{}", fmt::join(parts, "/"));

	return newPathStr;
}

bool Path::ValidateBounds() const {
	const auto normalized = Normalized();
	int depth             = 0;
	for (const auto& part : normalized) {
		if (part == "..") {
			if (depth == 0) { return false; }
			depth--;
		} else {
			depth++;
		}
	}

	return true;
}

std::string_view Path::Extension() const {
	if (_pathStr.empty()) { return {}; }

	const auto filename = Filename();
	const auto lastDot  = filename.find_last_of('.');
	if (lastDot == std::string::npos) { return {}; }

	return filename.substr(lastDot);
}

std::string_view Path::Filename() const {
	if (_pathStr.empty()) { return {}; }

	const auto filePath  = FilePath();
	const auto lastSlash = filePath.find_last_of('/');
	if (lastSlash == std::string::npos) { return filePath; }

	return filePath.substr(lastSlash + 1);
}

std::string_view Path::FilePath() const {
	const auto index = _pathStr.find("://");
	if (index == std::string::npos) { return _pathStr; }

	return std::string_view(_pathStr.begin() + index + 3, _pathStr.end());
}

std::string_view Path::ParentPath() const {
	if (_pathStr.empty() || IsRoot()) { return {}; }

	const auto filePath = FilePath();
	auto lastSlash      = filePath.find_last_of('/');
	if (lastSlash == std::string::npos) { return {}; }

	return filePath.substr(0, filePath.find_last_not_of('/', lastSlash) + 1);
}

std::string_view Path::Protocol() const {
	const auto index = _pathStr.find("://");
	if (index == std::string::npos) { return {}; }

	return std::string_view(_pathStr.begin(), _pathStr.begin() + index);
}

std::string_view Path::Stem() const {
	if (_pathStr.empty()) { return {}; }

	const auto filename = Filename();
	const auto lastDot  = filename.find_last_of('.');
	if (lastDot == std::string::npos) { return filename; }

	return filename.substr(0, lastDot);
}

Path::operator std::string() const {
	return _pathStr;
}

bool Path::operator==(const Path& other) const {
	return _pathStr == other._pathStr;
}

bool Path::operator!=(const Path& other) const {
	return _pathStr != other._pathStr;
}

Path Path::operator/(const char* path) const {
	return *this / Path(path);
}

Path Path::operator/(std::string_view path) const {
	return *this / Path(path);
}

Path Path::operator/(const Path& other) const {
	if (other.Empty()) { return *this; }
	if (other.IsRoot() || other.IsAbsolute()) { return other; }

	auto pathStr = _pathStr;
	if (!pathStr.ends_with('/')) { pathStr.push_back('/'); }
	pathStr.append(other._pathStr);

	return pathStr;
}

Path& Path::operator/=(const char* path) {
	return *this /= Path(path);
}

Path& Path::operator/=(std::string_view path) {
	return *this /= Path(path);
}

Path& Path::operator/=(const Path& other) {
	*this = *this / other;
	return *this;
}

PathIterator Path::begin() const {
	if (_pathStr.empty()) { return end(); }

	const auto protocolPosition = _pathStr.find("://");
	const bool hasProtocol      = protocolPosition != std::string::npos;
	auto start                  = _pathStr.cbegin();
	if (hasProtocol) { start += protocolPosition + 3; }

	const auto nextSlash = std::find(start, _pathStr.end(), '/');

	return PathIterator(start, std::string_view(start, nextSlash), this);
}

PathIterator Path::end() const noexcept {
	return PathIterator(_pathStr.cend(), this);
}

PathIterator::PathIterator() = default;

PathIterator::PathIterator(const BaseIteratorT& position, const Path* parent)
		: _position(position), _element(), _parent(parent) {}

PathIterator::PathIterator(const BaseIteratorT& position, std::string_view element, const Path* parent)
		: _position(position), _element(element), _parent(parent) {}

PathIterator& PathIterator::operator++() {
	const auto& str  = _parent->_pathStr;
	const auto size  = _element.size();
	const auto begin = str.begin();
	const auto end   = begin + str.size();

	if (*_position == '/') {
		if (size == 0) {
			++_position;

			return *this;
		}

		_position += size;
	} else {
		_position += size;
	}

	if (_position == end) {
		_element = {};
		return *this;
	}

	while (*_position == '/') {
		if (++_position == end) {
			--_position;
			_element = {};

			return *this;
		}
	}

	const auto nextSlash = std::find(_position, str.end(), '/');
	_element             = std::string_view(_position, nextSlash);

	return *this;
}

const std::string_view* PathIterator::operator->() const noexcept {
	return &_element;
}

const std::string_view& PathIterator::operator*() const noexcept {
	return _element;
}

bool PathIterator::operator==(const PathIterator& other) const {
	return _parent == other._parent && _position == other._position;
}

bool PathIterator::operator!=(const PathIterator& other) const {
	return _parent != other._parent || _position != other._position;
}
}  // namespace Luna
