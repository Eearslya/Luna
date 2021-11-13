#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <Luna/Core/Log.hpp>

namespace Luna {
namespace Log {
namespace _Internal {
static bool LoggerCreated = false;

static void Create() {
	auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>(spdlog::color_mode::always);
	consoleSink->set_pattern("%^[%T] %n-%L: %v%$");
	consoleSink->set_color(spdlog::level::critical, consoleSink->red_bold);
	consoleSink->set_color(spdlog::level::err, consoleSink->red);
	consoleSink->set_color(spdlog::level::warn, consoleSink->yellow);
	consoleSink->set_color(spdlog::level::info, consoleSink->cyan);
	consoleSink->set_color(spdlog::level::debug, consoleSink->magenta);
	consoleSink->set_color(spdlog::level::trace, consoleSink->white);

	auto fileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("Luna.log", true);
	fileSink->set_pattern("[%T] %n-%L: %v");

	auto logger = std::make_shared<spdlog::logger>("Luna", spdlog::sinks_init_list{consoleSink, fileSink});
	spdlog::register_logger(logger);

	LoggerCreated = true;
}

spdlog::logger& Get() {
	if (!LoggerCreated) { Create(); }

	return *spdlog::get("Luna");
}
}  // namespace _Internal

void SetLevel(spdlog::level::level_enum level) {
	_Internal::Get().set_level(level);
}
}  // namespace Log
}  // namespace Luna
