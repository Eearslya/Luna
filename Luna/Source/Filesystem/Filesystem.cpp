#include <physfs.h>

#include <Luna/Core/Log.hpp>
#include <Luna/Filesystem/Filesystem.hpp>
#include <fstream>

namespace Luna {
class FileBuffer : public std::streambuf, NonCopyable {
 public:
	explicit FileBuffer(PHYSFS_File* file, std::size_t bufferSize = 2048) : _bufferSize(bufferSize), _file(file) {
		_buffer  = new char[bufferSize];
		auto end = _buffer + _bufferSize;
		setg(end, end, end);
		setp(_buffer, end);
	}

	~FileBuffer() noexcept {
		sync();
		delete[] _buffer;
	}

 protected:
	PHYSFS_File* _file = nullptr;

 private:
	int_type overflow(int_type c = traits_type::eof()) override {
		if (pptr() == pbase() && c == traits_type::eof()) { return 0; }

		if (PHYSFS_writeBytes(_file, pbase(), static_cast<PHYSFS_uint32>(pptr() - pbase())) < 1) {
			return traits_type::eof();
		}

		if (c != traits_type::eof()) {
			if (PHYSFS_writeBytes(_file, &c, 1) < 1) { return traits_type::eof(); }
		}

		return 0;
	}

	pos_type seekoff(off_type pos, std::ios_base::seekdir dir, std::ios_base::openmode mode) override {
		switch (dir) {
			case std::ios_base::beg:
				PHYSFS_seek(_file, pos);
				break;
			case std::ios_base::cur:
				PHYSFS_seek(_file, (PHYSFS_tell(_file) + pos) - (egptr() - gptr()));
				break;
			case std::ios_base::end:
				PHYSFS_seek(_file, PHYSFS_fileLength(_file) + pos);
				break;
		}

		if (mode & std::ios_base::in) { setg(egptr(), egptr(), egptr()); }
		if (mode & std::ios_base::out) { setp(_buffer, _buffer); }

		return PHYSFS_tell(_file);
	}

	pos_type seekpos(pos_type pos, std::ios_base::openmode mode) override {
		PHYSFS_seek(_file, pos);

		if (mode & std::ios_base::in) { setg(egptr(), egptr(), egptr()); }
		if (mode & std::ios_base::out) { setp(_buffer, _buffer); }

		return PHYSFS_tell(_file);
	}

	int sync() override {
		return overflow();
	}

	int_type underflow() override {
		if (PHYSFS_eof(_file)) { return traits_type::eof(); }

		const auto bytesRead = PHYSFS_readBytes(_file, _buffer, static_cast<PHYSFS_uint32>(_bufferSize));
		if (bytesRead < 1) { return traits_type::eof(); }

		setg(_buffer, _buffer, _buffer + static_cast<size_t>(bytesRead));

		return static_cast<int_type>(*gptr());
	}

	char* _buffer      = nullptr;
	size_t _bufferSize = 0;
};

static PHYSFS_File* OpenFile(const std::filesystem::path& filename, FileMode openMode) {
	PHYSFS_File* file = nullptr;

	auto pathStr = filename.string();
	std::replace(pathStr.begin(), pathStr.end(), '\\', '/');

	switch (openMode) {
		case FileMode::Write:
			file = PHYSFS_openWrite(pathStr.c_str());
			break;
		case FileMode::Append:
			file = PHYSFS_openAppend(pathStr.c_str());
			break;
		case FileMode::Read:
			file = PHYSFS_openRead(pathStr.c_str());
			break;
	}

	if (file == nullptr) { throw std::invalid_argument("Could not open file!"); }

	return file;
}

BaseFileStream::BaseFileStream(PHYSFS_File* file) : _file(file) {
	if (_file == nullptr) { throw std::invalid_argument("BaseFileStream cannot be initialized with a null file!"); }
}

BaseFileStream::~BaseFileStream() noexcept {
	PHYSFS_close(_file);
}

size_t BaseFileStream::Length() const {
	return PHYSFS_fileLength(_file);
}

IFileStream::IFileStream(const std::filesystem::path& filename)
		: BaseFileStream(OpenFile(filename, FileMode::Read)), std::istream(new FileBuffer(_file)) {}

IFileStream::~IFileStream() noexcept {
	delete rdbuf();
}

OFileStream::OFileStream(const std::filesystem::path& filename, FileMode writeMode)
		: BaseFileStream(OpenFile(filename, writeMode)), std::ostream(new FileBuffer(_file)) {}

OFileStream::~OFileStream() noexcept {
	delete rdbuf();
}

FileStream::FileStream(const std::filesystem::path& filename, FileMode openMode)
		: BaseFileStream(OpenFile(filename, openMode)), std::iostream(new FileBuffer(_file)) {}

FileStream::~FileStream() noexcept {
	delete rdbuf();
}

Filesystem::Filesystem() {
	if (PHYSFS_init(Engine::Get()->GetArgv0().c_str()) == 0) {
		throw std::runtime_error("Failed to initialize PhysicsFS!");
	}
}

Filesystem::~Filesystem() noexcept {
	PHYSFS_deinit();
}

void Filesystem::Update() {}

void Filesystem::AddSearchPath(const std::string& path) {
	if (std::find(_searchPaths.begin(), _searchPaths.end(), path) != _searchPaths.end()) { return; }

	if (PHYSFS_mount(path.c_str(), nullptr, true) == 0) {
		Log::Warning("Filesystem", "Failed to mount search path '{}'.", path);
		return;
	}

	_searchPaths.emplace_back(path);
}

void Filesystem::ClearSearchPaths() {
	for (const auto& path : _searchPaths) { RemoveSearchPath(path); }
	_searchPaths.clear();
}

bool Filesystem::Exists(const std::filesystem::path& path) {
	auto pathStr = path.string();
	std::replace(pathStr.begin(), pathStr.end(), '\\', '/');

	return PHYSFS_exists(pathStr.c_str()) != 0;
}

std::vector<std::string> Filesystem::Files(const std::filesystem::path& path, bool recursive) {
	auto pathStr = path.string();
	std::replace(pathStr.begin(), pathStr.end(), '\\', '/');
	auto rc = PHYSFS_enumerateFiles(pathStr.c_str());

	std::vector<std::string> files;
	for (auto i = rc; *i; ++i) { files.emplace_back(*i); }

	PHYSFS_freeList(rc);

	return files;
}

void Filesystem::RemoveSearchPath(const std::string& path) {
	auto it = std::find(_searchPaths.begin(), _searchPaths.end(), path);
	if (it == _searchPaths.end()) { return; }

	if (PHYSFS_unmount(path.c_str()) == 0) { Log::Warning("Filesystem", "Failed to unmount search path '{}'.", path); }

	_searchPaths.erase(it);
}

std::optional<std::string> Filesystem::Read(const std::filesystem::path& path) {
	auto pathStr = path.string();
	std::replace(pathStr.begin(), pathStr.end(), '\\', '/');
	auto fsFile = PHYSFS_openRead(pathStr.c_str());

	if (!fsFile) {
		if (!std::filesystem::exists(path) || !std::filesystem::is_regular_file(path)) {
			Log::Error(
				"Filesystem", "Failed to open file '{}': {}", pathStr, PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
			return std::nullopt;
		}

		std::ifstream is(path);
		std::stringstream buffer;
		buffer << is.rdbuf();

		return buffer.str();
	}

	const auto size = PHYSFS_fileLength(fsFile);
	std::vector<uint8_t> data(size);
	PHYSFS_readBytes(fsFile, data.data(), static_cast<PHYSFS_uint64>(size));

	if (PHYSFS_close(fsFile) == 0) {
		Log::Warning(
			"Filesystem", "Failed to close file '{}': {}", pathStr, PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
	}

	return std::string(data.begin(), data.end());
}

std::optional<std::vector<uint8_t>> Filesystem::ReadBytes(const std::filesystem::path& path) {
	auto pathStr = path.string();
	std::replace(pathStr.begin(), pathStr.end(), '\\', '/');
	auto fsFile = PHYSFS_openRead(pathStr.c_str());

	if (!fsFile) {
		if (!std::filesystem::exists(path) || !std::filesystem::is_regular_file(path)) {
			Log::Error(
				"Filesystem", "Failed to open file '{}': {}", pathStr, PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
			return std::nullopt;
		}

		std::ifstream is(path, std::ios::ate);
		const auto fileSize = is.tellg();
		std::vector<uint8_t> bytes(fileSize);
		is.read(reinterpret_cast<char*>(bytes.data()), fileSize);

		return bytes;
	}

	const auto size = PHYSFS_fileLength(fsFile);
	std::vector<uint8_t> data(size);
	PHYSFS_readBytes(fsFile, data.data(), static_cast<PHYSFS_uint64>(size));

	if (PHYSFS_close(fsFile) == 0) {
		Log::Warning(
			"Filesystem", "Failed to close file '{}': {}", pathStr, PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
	}

	return data;
}
}  // namespace Luna
