#pragma once

#include <spdlog/spdlog.h>

namespace Luna {
class Log {
 public:
	enum class Level { Fatal, Error, Warning, Info, Debug, Trace };

	// Initialize our logging system. This must be called before any log calls.
	static void Initialize();

	// Shutdown and free our logging system.
	static void Shutdown();

	// Set the minimum level the log system will output.
	static void SetLevel(Level level) {
		if (!_mainLogger) { return; }

		_mainLogger->set_level(ConvertLevel(level));
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
	// Format and write our log message out to spdlog.
	template <typename... Args>
	static void Output(Level level, std::string_view tag, const fmt::format_string<Args...>& format, Args&&... args) {
		if (!_mainLogger) { return; }

		const auto logMessage = fmt::format(format, std::forward<Args>(args)...);
		_mainLogger->log(ConvertLevel(level), "[{}] {}", tag, logMessage);
	}

	// Convert our logging level to one of spdlog's.
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

	static std::shared_ptr<spdlog::logger> _mainLogger;
};
}  // namespace Luna
