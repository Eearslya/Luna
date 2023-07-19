#include <spdlog/async.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <Luna/Core/Log.hpp>

namespace Luna {
bool Log::Initialize() {
	spdlog::init_thread_pool(8192, 1);

	const std::filesystem::path logsDirectory = "Logs";
	if (!std::filesystem::exists(logsDirectory)) { std::filesystem::create_directories(logsDirectory); }
	const auto logFile = logsDirectory / "Luna.log";

	auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
	auto fileSink    = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logFile.string(), true);

	consoleSink->set_pattern("%^[%T] %n-%L: %v%$");
	fileSink->set_pattern("[%T] %n-%L: %v");

	std::vector<spdlog::sink_ptr> sinks{consoleSink, fileSink};
	auto logger = std::make_shared<spdlog::async_logger>(
		"Luna", sinks.begin(), sinks.end(), spdlog::thread_pool(), spdlog::async_overflow_policy::block);
	spdlog::set_default_logger(logger);

	return true;
}

void Log::Shutdown() {
	spdlog::shutdown();
}

Log::Level Log::GetLevel() {
	return FromSpdlog(spdlog::get_level());
}

void Log::SetLevel(Level level) {
	spdlog::set_level(ToSpdlog(level));
}

Log::Level Log::FromSpdlog(spdlog::level::level_enum level) {
	switch (level) {
		case spdlog::level::critical:
			return Level::Fatal;
		case spdlog::level::err:
			return Level::Error;
		case spdlog::level::warn:
			return Level::Warning;
		case spdlog::level::info:
			return Level::Info;
		case spdlog::level::debug:
			return Level::Debug;
		case spdlog::level::trace:
			return Level::Trace;
		default:
			return Level::Info;
	}
}

spdlog::level::level_enum Log::ToSpdlog(Level level) {
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
}
}  // namespace Luna
