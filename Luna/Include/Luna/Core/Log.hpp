#pragma once

#include <spdlog/spdlog.h>

#include <Luna/Common.hpp>

namespace Luna {
class Log {
 public:
	enum class Level { Fatal, Error, Warning, Info, Debug, Trace };

	static bool Initialize();
	static void Shutdown();

	static Level GetLevel();
	static void SetLevel(Level level);

	template <typename... Args>
	static void Assert(bool condition, std::string_view tag, const fmt::format_string<Args...>& format, Args&&... args) {
		if (!condition) {
			Output(Level::Fatal, tag, format, std::forward<Args>(args)...);
			Shutdown();
			std::exit(-1);
		}
	}
	template <typename... Args>
	static void Fatal(std::string_view tag, const fmt::format_string<Args...>& format, Args&&... args) {
		Output(Level::Fatal, tag, format, std::forward<Args>(args)...);
	}
	template <typename... Args>
	static void Error(std::string_view tag, const fmt::format_string<Args...>& format, Args&&... args) {
		Output(Level::Error, tag, format, std::forward<Args>(args)...);
	}
	template <typename... Args>
	static void Warning(std::string_view tag, const fmt::format_string<Args...>& format, Args&&... args) {
		Output(Level::Warning, tag, format, std::forward<Args>(args)...);
	}
	template <typename... Args>
	static void Info(std::string_view tag, const fmt::format_string<Args...>& format, Args&&... args) {
		Output(Level::Info, tag, format, std::forward<Args>(args)...);
	}
	template <typename... Args>
	static void Debug(std::string_view tag, const fmt::format_string<Args...>& format, Args&&... args) {
		Output(Level::Debug, tag, format, std::forward<Args>(args)...);
	}
	template <typename... Args>
	static void Trace(std::string_view tag, const fmt::format_string<Args...>& format, Args&&... args) {
		Output(Level::Trace, tag, format, std::forward<Args>(args)...);
	}

 private:
	static Level FromSpdlog(spdlog::level::level_enum level);
	static spdlog::level::level_enum ToSpdlog(Level level);

	template <typename... Args>
	static void Output(Level level, std::string_view tag, const fmt::format_string<Args...>& format, Args&&... args) {
		const auto logMessage = fmt::format(format, std::forward<Args>(args)...);
		spdlog::log(ToSpdlog(level), "[{}] {}", tag, logMessage);
	}
};
}  // namespace Luna
