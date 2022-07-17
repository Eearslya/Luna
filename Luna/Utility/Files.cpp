#include "Files.hpp"

#include <fstream>
#include <sstream>

namespace Luna {
std::string ReadFile(const std::filesystem::path& filePath) {
	std::ifstream file(filePath);
	if (!file.is_open()) { throw std::runtime_error("Failed to open file for reading!"); }

	std::stringstream ss;
	ss << file.rdbuf();

	return ss.str();
}
}  // namespace Luna
