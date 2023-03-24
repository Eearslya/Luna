#pragma once

#include <string>
#include <vector>

namespace Luna {
std::vector<std::string> StringSplit(const std::string& str, const char* delim, bool keepEmpty = true);
}
