#pragma once

#include <spdlog/spdlog.h>

namespace Luna {
class Log {
 public:
	enum class Level { Fatal, Error, Warning, Info, Debug, Trace };

	static void SetLevel(Level level) {
		if (!_mainLogger) { return; }

		_mainLogger->set_level(ConvertLevel(level));
	}

	static void Initialize();
	static void Shutdown();

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
	static spdlog::level::level_enum ConvertLevel(Level level) {
		switch (level) {
			case Level::Fatal:
				return spdlog::level::critical;
			case Level::Error:
				return spdlog::level::err;
			case Level::Warning:
				return spdlog::level::warn;
			case Level::Info:
				return spdlog::level::info;
			case Level::Debug:
				return spdlog::level::debug;
			case Level::Trace:
				return spdlog::level::trace;
		}

		return spdlog::level::info;
	}

	template <typename... Args>
	static void Output(Level level, std::string_view tag, const fmt::format_string<Args...>& format, Args&&... args) {
		if (!_mainLogger) { return; }

		const auto logMessage = fmt::format(format, std::forward<Args>(args)...);
		_mainLogger->log(ConvertLevel(level), "[{}] {}", tag, logMessage);
	}

	static std::shared_ptr<spdlog::logger> _mainLogger;
};
}  // namespace Luna

#define LAssert(cond)                                                                \
	do {                                                                               \
		if (!(cond)) {                                                                   \
			::Luna::Log::Fatal("Luna", "Assertion failed: {}", #cond);                     \
			::Luna::Log::Fatal("Luna", "- {} L{} ({})", __FILE__, __LINE__, __FUNCTION__); \
			::Luna::Log::Shutdown();                                                       \
			abort();                                                                       \
		}                                                                                \
	} while (0)

#define LAssertMsg(cond, tag, msg, ...)                                                 \
	do {                                                                                  \
		if (!(cond)) {                                                                      \
			::Luna::Log::Fatal(tag, "Assertion failed: {}", fmt::format(msg, ##__VA_ARGS__)); \
			::Luna::Log::Fatal(tag, "- {} L{} ({})", __FILE__, __LINE__, __FUNCTION__);       \
			::Luna::Log::Shutdown();                                                          \
			abort();                                                                          \
		}                                                                                   \
	} while (0)
