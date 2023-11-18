#pragma once

#include <Luna/Common.hpp>

namespace Luna {
template<typename Range, typename Value = typename Range::value_type>
std::string StringJoin(const Range& elements, const char* const delimiter) {
  std::ostringstream oss;
  auto b = std::begin(elements);
  auto e = std::end(elements);

  if (b != e) {
    std::copy(b, std::prev(e), std::ostream_iterator<Value>(oss, delimiter));
    b = std::prev(e);
  }
  if (b != e) {
    oss << *b;
  }

  return oss.str();
}

std::vector<std::string> StringSplit(std::string_view str, std::string_view delim, bool keepEmpty = true);
}
