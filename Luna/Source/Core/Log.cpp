#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <Luna/Core/Log.hpp>

namespace Luna {
namespace Log {
static bool LoggerCreated = false;

static void Create() {
#ifdef _MSC_VER
	auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
#else
	auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>(spdlog::color_mode::always);
#endif

	consoleSink->set_pattern("%^[%T] %n-%L: %v%$");

#ifdef _MSC_VER
	consoleSink->set_color(spdlog::level::critical, 4);  // Red
	consoleSink->set_color(spdlog::level::err, 4);       // Red
	consoleSink->set_color(spdlog::level::warn, 14);     // Yellow
	consoleSink->set_color(spdlog::level::info, 3);      // Cyan
	consoleSink->set_color(spdlog::level::debug, 5);     // Magenta
	consoleSink->set_color(spdlog::level::trace, 15);    // White
#else
	consoleSink->set_color(spdlog::level::critical, consoleSink->red_bold);
	consoleSink->set_color(spdlog::level::err, consoleSink->red);
	consoleSink->set_color(spdlog::level::warn, consoleSink->yellow);
	consoleSink->set_color(spdlog::level::info, consoleSink->cyan);
	consoleSink->set_color(spdlog::level::debug, consoleSink->magenta);
	consoleSink->set_color(spdlog::level::trace, consoleSink->white);
#endif

	auto fileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("Luna.log", true);
	fileSink->set_pattern("[%T] %n-%L: %v");

	auto logger = std::make_shared<spdlog::logger>("Luna", spdlog::sinks_init_list{consoleSink, fileSink});
	spdlog::register_logger(logger);

	LoggerCreated = true;
}

static spdlog::logger& GetLogger() {
	if (!LoggerCreated) { Create(); }

	return *spdlog::get("Luna");
}

void Output(Level level, std::string_view tag, std::string_view msg) {
	switch (level) {
		case Level::Fatal:
			GetLogger().critical("[{}] {}", tag, msg);
			break;
		case Level::Error:
			GetLogger().error("[{}] {}", tag, msg);
			break;
		case Level::Warning:
			GetLogger().warn("[{}] {}", tag, msg);
			break;
		case Level::Info:
			GetLogger().info("[{}] {}", tag, msg);
			break;
		case Level::Debug:
			GetLogger().debug("[{}] {}", tag, msg);
			break;
		case Level::Trace:
			GetLogger().trace("[{}] {}", tag, msg);
			break;
	}
}

void SetLevel(Level level) noexcept {
	spdlog::level::level_enum newLevel = spdlog::level::info;
	switch (level) {
		case Level::Fatal:
			newLevel = spdlog::level::critical;
			break;
		case Level::Error:
			newLevel = spdlog::level::err;
			break;
		case Level::Warning:
			newLevel = spdlog::level::warn;
			break;
		case Level::Debug:
			newLevel = spdlog::level::debug;
			break;
		case Level::Trace:
			newLevel = spdlog::level::trace;
			break;
		default:
			newLevel = spdlog::level::info;
			break;
	}
	GetLogger().set_level(newLevel);
}
}  // namespace Log
}  // namespace Luna
