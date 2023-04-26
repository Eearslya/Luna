#include <Luna/Utility/Log.hpp>
#include <Luna/Utility/Path.hpp>
#include <algorithm>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

namespace Luna {
Path::Path() {}

Path::Path(const char* pathStr) : Path(std::string(pathStr)) {}

Path::Path(const std::string& pathStr) : _pathStr(pathStr) {
	for (auto& c : _pathStr) {
		if (c == '\\') { c = '/'; }
	}
}

Path::Path(const std::filesystem::path& fsPath) : Path(fsPath.string()) {}

bool Path::IsAbsolute() const {
	if (_pathStr.empty()) { return false; }
	if (_pathStr.front() == '/') { return true; }

	return _pathStr.find("://") != std::string::npos;
}

bool Path::IsRoot() const {
	if (_pathStr.empty()) { return false; }
	if (_pathStr.front() == '/' && _pathStr.size() == 1) { return true; }

	const auto pos = _pathStr.find("://");
	return pos != std::string::npos && (pos + 3) == _pathStr.size();
}

Path Path::BaseDirectory() const {
	if (_pathStr.empty()) { return ""; }
	if (IsRoot()) { return _pathStr; }

	auto pos = _pathStr.find_last_of('/');
	if (pos == std::string::npos) { return "."; }

	if (pos == 0 && IsAbsolute()) { ++pos; }

	auto ret     = _pathStr.substr(0, pos + 1);
	auto retPath = Path(ret);
	if (!retPath.IsRoot()) { ret.pop_back(); }

	return Path(ret);
}

std::string Path::Extension() const {
	const auto pos = _pathStr.find_last_of('.');
	if (pos == std::string::npos) { return ""; }

	return _pathStr.substr(pos + 1);
}

std::string Path::Filename() const {
	if (_pathStr.empty()) { return ""; }

	auto pos = _pathStr.find_last_of('/');
	if (pos == std::string::npos) { return _pathStr; }

	return _pathStr.substr(pos + 1);
}

Path Path::Join(const Path& path) const {
	if (_pathStr.empty()) { return path; }
	if (path._pathStr.empty()) { return *this; }
	if (path.IsAbsolute()) { return path; }

	auto pos                  = _pathStr.find_last_of('/');
	const bool needsSlash     = pos != _pathStr.size() - 1;
	const std::string newPath = fmt::format("{}{}{}", _pathStr, needsSlash ? "/" : "", path._pathStr);

	return Path(newPath);
}

std::string Path::Protocol() const {
	return ProtocolSplit().first;
}

std::pair<std::string, std::string> Path::ProtocolSplit() const {
	if (_pathStr.empty()) { return {}; }

	const auto pos = _pathStr.find("://");
	if (pos == std::string::npos) { return {"", _pathStr}; }

	return {_pathStr.substr(0, pos), _pathStr.substr(pos + 3)};
}

Path Path::Relative(const Path& other) const {
	return BaseDirectory() / other;
}

std::string Path::Stem() const {
	auto filename = Filename();

	auto pos = filename.find_last_of('.');
	if (pos == std::string::npos) { return filename; }

	return filename.substr(0, pos);
}

std::string Path::WithoutProtocol() const {
	return ProtocolSplit().second;
}

const std::string& Path::String() const {
	return _pathStr;
}

std::wstring Path::WString() const {
	auto ret = MultiByteToWideChar(CP_UTF8, 0, _pathStr.data(), _pathStr.size(), nullptr, 0);
	if (ret < 0) { return L""; }

	std::vector<wchar_t> buffer(ret);
	MultiByteToWideChar(CP_UTF8, 0, _pathStr.data(), _pathStr.size(), buffer.data(), buffer.size());

	return std::wstring(buffer.begin(), buffer.end());
}
}  // namespace Luna
