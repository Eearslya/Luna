#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/ringbuffer_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <Luna/Core/Log.hpp>

namespace Luna {
namespace Log {
namespace _Internal {
static bool LoggerCreated = false;
static std::shared_ptr<spdlog::sinks::ringbuffer_sink_mt> RingSink;

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

	RingSink = std::make_shared<spdlog::sinks::ringbuffer_sink_mt>(8192);
	RingSink->set_pattern("[%T] %n-%L: %v");

	auto logger = std::make_shared<spdlog::logger>("Luna", spdlog::sinks_init_list{consoleSink, fileSink, RingSink});
	spdlog::register_logger(logger);

	LoggerCreated = true;
}

spdlog::logger& Get() {
	if (!LoggerCreated) { Create(); }

	return *spdlog::get("Luna");
}
}  // namespace _Internal

std::vector<std::string> GetLast(size_t count) {
	return _Internal::RingSink->last_formatted(count);
}

void SetLevel(spdlog::level::level_enum level) {
	_Internal::Get().set_level(level);
}
}  // namespace Log
}  // namespace Luna
