#include <ShlObj.h>

#include <Luna/Core/Engine.hpp>
#include <Luna/Core/Log.hpp>
#include <Luna/Filesystem/Filesystem.hpp>
#include <Luna/Utility/NonCopyable.hpp>
#include <fstream>

namespace Luna {
Filesystem* Filesystem::_instance = nullptr;

Filesystem::Filesystem() {
	if (_instance) { throw std::runtime_error("Filesystem was initialized twice!"); }
	_instance = this;
}

Filesystem::~Filesystem() noexcept {
	_instance = nullptr;
}

std::filesystem::path Filesystem::GetSpecialFolder(SpecialFolder folder) const {
	PWSTR resultPath = nullptr;
	GUID folderId;
	switch (folder) {
		case SpecialFolder::Documents:
			folderId = FOLDERID_Documents;
			break;
		case SpecialFolder::ApplicationData:
			folderId = FOLDERID_AppDataProgramData;
			break;
		default:
			throw std::runtime_error("Invalid SpecialFolder type!");
	}
	const HRESULT result = ::SHGetKnownFolderPath(folderId, KF_FLAG_DEFAULT, nullptr, &resultPath);

	return std::filesystem::path(std::wstring(resultPath));
}

void Filesystem::AddSearchPath(const std::string& path) {
	if (std::find(_searchPaths.begin(), _searchPaths.end(), path) != _searchPaths.end()) { return; }

	_searchPaths.emplace_back(path);
}

void Filesystem::ClearSearchPaths() {
	for (const auto& path : _searchPaths) { RemoveSearchPath(path); }
	_searchPaths.clear();
}

bool Filesystem::Exists(const std::filesystem::path& path) {
	const auto actualPath = FindPath(path);

	return !actualPath.empty();
}

std::vector<std::string> Filesystem::Files(const std::filesystem::path& path, bool recursive) {
	const auto actualPath = FindPath(path);

	std::vector<std::string> files;
	for (auto& entry : std::filesystem::directory_iterator(actualPath)) { files.emplace_back(entry.path().string()); }

	return files;
}

void Filesystem::RemoveSearchPath(const std::string& path) {
	auto it = std::find(_searchPaths.begin(), _searchPaths.end(), path);
	if (it == _searchPaths.end()) { return; }

	_searchPaths.erase(it);
}

std::optional<std::string> Filesystem::Read(const std::filesystem::path& path) {
	const auto actualPath = FindPath(path);
	if (actualPath.empty()) {
		Log::Error("Filesystem", "Failed to read file '{}': File could not be found in any search path.", path.string());
		return std::nullopt;
	}

	try {
		std::ifstream file(actualPath, std::ios::ate);
		if (!file.is_open()) {
			Log::Error("Filesystem", "Failed to read file '{}': Could not open file.", actualPath.string());
			return std::nullopt;
		}
		const size_t fileSize = file.tellg();
		std::string ret(fileSize, ' ');
		file.seekg(0);
		file.read(ret.data(), fileSize);
		return ret;
	} catch (const std::exception& e) {
		Log::Error("Filesystem", "Failed to read file '{}': {}", actualPath.string(), e.what());
		return std::nullopt;
	}
}

std::optional<std::vector<uint8_t>> Filesystem::ReadBytes(const std::filesystem::path& path) {
	const auto actualPath = FindPath(path);
	if (actualPath.empty()) {
		Log::Error(
			"Filesystem", "Failed to read binary file '{}': File could not be found in any search path.", path.string());
		return std::nullopt;
	}

	try {
		std::ifstream file(actualPath, std::ios::ate);
		if (!file.is_open()) {
			Log::Error("Filesystem", "Failed to read binary file '{}': Could not open file.", actualPath.string());
			return std::nullopt;
		}
		const auto fileSize = file.tellg();
		std::vector<uint8_t> bytes(fileSize);
		file.read(reinterpret_cast<char*>(bytes.data()), fileSize);
		return bytes;
	} catch (const std::exception& e) {
		Log::Error("Filesystem", "Failed to read binary file '{}': {}", actualPath.string(), e.what());
		return std::nullopt;
	}

	return std::nullopt;
}

std::filesystem::path Filesystem::FindPath(const std::filesystem::path& path) const {
	if (path.is_absolute() && std::filesystem::exists(path)) { return path; }
	for (const auto& search : _searchPaths) {
		const auto relPath = search / path;
		if (std::filesystem::exists(relPath)) { return relPath; }
	}

	return {};
}
}  // namespace Luna
