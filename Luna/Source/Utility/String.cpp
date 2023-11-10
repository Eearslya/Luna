#include <Luna/Utility/String.hpp>

namespace Luna {
std::vector<std::string> StringSplit(std::string_view str, std::string_view delim, bool keepEmpty) {
	if (str.empty()) { return {}; }

	std::vector<std::string> ret;

	std::string::size_type startIndex = 0;
	std::string::size_type index      = 0;
	while ((index = str.find_first_of(delim, startIndex)) != std::string::npos) {
		if (keepEmpty || index > startIndex) { ret.emplace_back(str.substr(startIndex, index - startIndex)); }
		startIndex = index + 1;
		if (keepEmpty && (index == str.size() - 1)) { ret.emplace_back(); }
	}

	if (startIndex < str.size()) { ret.emplace_back(str.substr(startIndex)); }

	return ret;
}
}  // namespace Luna
