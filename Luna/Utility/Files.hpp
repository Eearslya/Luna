#pragma once

#include <filesystem>

namespace Luna {
std::string ReadFile(const std::filesystem::path& filePath);
std::vector<uint8_t> ReadFileBinary(const std::filesystem::path& filePath);
}  // namespace Luna
