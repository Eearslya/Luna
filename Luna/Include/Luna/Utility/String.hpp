#pragma once

#include <Luna/Common.hpp>

namespace Luna {
std::vector<std::string> StringSplit(std::string_view str, std::string_view delim, bool keepEmpty = true);
}
