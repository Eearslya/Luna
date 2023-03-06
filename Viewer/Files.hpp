#pragma once

#include <filesystem>
#include <string>
#include <vector>

std::string ReadFile(const std::filesystem::path& filePath);
std::vector<uint8_t> ReadFileBinary(const std::filesystem::path& filePath);
