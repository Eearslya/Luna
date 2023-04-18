#include <Luna/Assets/AssetFile.hpp>
#include <Luna/Platform/Filesystem.hpp>

namespace Luna {
constexpr uint8_t MagicNumber[] = {'L', 'U', 'N', 'A'};

struct FileHeader {
	uint8_t Magic[4];
	uint32_t FileVersion;
	uint32_t AssetType;
};

bool AssetFile::Load(const Path& path) {
	auto* backend = Filesystem::GetBackend("project");
	auto file     = backend->Open(path);
	if (!file) { return false; }
	auto fileMap = file->Map();
	if (!fileMap) { return false; }

	size_t cursor   = 0;
	const auto Read = [&](void* dst, size_t size) {
		memcpy(dst, fileMap->Data<char>() + cursor, size);
		cursor += size;
	};

	FileHeader header;
	Read(&header, sizeof(header));
	for (int i = 0; i < 4; ++i) {
		if (header.Magic[i] != MagicNumber[i]) { return false; }
	}
	FileVersion = header.FileVersion;
	Type        = static_cast<AssetType>(header.AssetType);

	uint32_t jsonSize = 0;
	Read(&jsonSize, sizeof(uint32_t));
	Json.resize(jsonSize);

	uint32_t binarySize = 0;
	Read(&binarySize, sizeof(uint32_t));
	Binary.resize(binarySize);

	Read(Json.data(), jsonSize);
	Read(Binary.data(), binarySize);

	return true;
}

bool AssetFile::Save(const Path& path) {
	FileHeader header;
	memcpy(header.Magic, MagicNumber, sizeof(MagicNumber));
	header.FileVersion = FileVersion;
	header.AssetType   = uint32_t(Type);

	const uint32_t headerSize = sizeof(header);
	const uint32_t jsonSize   = Json.size() + sizeof(uint32_t);
	const uint32_t binarySize = Binary.size() + sizeof(uint32_t);
	const uint32_t fileSize   = headerSize + jsonSize + binarySize;

	auto* backend = Filesystem::GetBackend("project");
	auto file     = backend->Open(path, FileMode::WriteOnly);
	if (!file) { return false; }
	auto fileMap = file->MapWrite(fileSize);
	if (!fileMap) { return false; }

	size_t cursor    = 0;
	const auto Write = [&](const void* src, size_t size) {
		memcpy(fileMap->MutableData<char>() + cursor, src, size);
		cursor += size;
	};

	Write(&header, sizeof(header));
	Write(reinterpret_cast<const char*>(&jsonSize), sizeof(uint32_t));
	Write(reinterpret_cast<const char*>(&binarySize), sizeof(uint32_t));
	Write(Json.data(), Json.size());
	Write(reinterpret_cast<const char*>(Binary.data()), Binary.size());

	return true;
}
}  // namespace Luna
