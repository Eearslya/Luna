#pragma once

#include <spdlog/fmt/fmt.h>

namespace Luna {
namespace Log {
enum class Level { Fatal, Error, Warning, Info, Debug, Trace };

void Output(Level level, std::string_view tag, std::string_view msg);
void SetLevel(Level level) noexcept;

template <typename... Args>
void Fatal(std::string_view tag, fmt::format_string<Args...> format, Args&&... args) {
	Output(Level::Fatal, tag, fmt::format(format, std::forward<Args>(args)...));
}

template <typename... Args>
void Error(std::string_view tag, fmt::format_string<Args...> format, Args&&... args) {
	Output(Level::Error, tag, fmt::format(format, std::forward<Args>(args)...));
}

template <typename... Args>
void Warning(std::string_view tag, fmt::format_string<Args...> format, Args&&... args) {
	Output(Level::Warning, tag, fmt::format(format, std::forward<Args>(args)...));
}

template <typename... Args>
void Info(std::string_view tag, fmt::format_string<Args...> format, Args&&... args) {
	Output(Level::Info, tag, fmt::format(format, std::forward<Args>(args)...));
}

template <typename... Args>
void Debug(std::string_view tag, fmt::format_string<Args...> format, Args&&... args) {
	Output(Level::Debug, tag, fmt::format(format, std::forward<Args>(args)...));
}

template <typename... Args>
void Trace(std::string_view tag, fmt::format_string<Args...> format, Args&&... args) {
	Output(Level::Trace, tag, fmt::format(format, std::forward<Args>(args)...));
}
}  // namespace Log
}  // namespace Luna
