#include "Files.hpp"

#include <fstream>
#include <sstream>

std::string ReadFile(const std::filesystem::path& filePath) {
	std::ifstream file(filePath);
	if (!file.is_open()) { throw std::runtime_error("Failed to open file for reading!"); }

	std::stringstream ss;
	ss << file.rdbuf();

	return ss.str();
}

std::vector<uint8_t> ReadFileBinary(const std::filesystem::path& filePath) {
	std::ifstream file(filePath, std::ios::ate | std::ios::binary);
	if (!file.is_open()) { throw std::runtime_error("Failed to open file for reading!"); }

	const size_t fileSize = file.tellg();
	file.seekg(0);
	std::vector<uint8_t> bytes(fileSize);
	file.read(reinterpret_cast<char*>(bytes.data()), fileSize);

	return bytes;
}
