#pragma once

#include <spdlog/spdlog.h>

#include <string>
#include <vector>

namespace Luna {
namespace Log {
namespace _Internal {
spdlog::logger& Get();
}

std::vector<std::string> GetLast(size_t count);
void SetLevel(spdlog::level::level_enum level);

template <typename... Args>
void Fatal(fmt::format_string<Args...> format, Args&&... args) {
	_Internal::Get().critical(format, std::forward<Args>(args)...);
}
template <typename... Args>
void Error(fmt::format_string<Args...> format, Args&&... args) {
	_Internal::Get().error(format, std::forward<Args>(args)...);
}
template <typename... Args>
void Warning(fmt::format_string<Args...> format, Args&&... args) {
	_Internal::Get().warn(format, std::forward<Args>(args)...);
}
template <typename... Args>
void Info(fmt::format_string<Args...> format, Args&&... args) {
	_Internal::Get().info(format, std::forward<Args>(args)...);
}
template <typename... Args>
void Debug(fmt::format_string<Args...> format, Args&&... args) {
	_Internal::Get().debug(format, std::forward<Args>(args)...);
}
template <typename... Args>
void Trace(fmt::format_string<Args...> format, Args&&... args) {
	_Internal::Get().trace(format, std::forward<Args>(args)...);
}
}  // namespace Log
}  // namespace Luna
