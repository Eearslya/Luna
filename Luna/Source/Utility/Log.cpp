#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <Luna/Utility/Log.hpp>
#include <Tracy/Tracy.hpp>
#include <filesystem>

namespace Luna {
std::shared_ptr<spdlog::logger> Log::_mainLogger;

bool Log::Initialize() {
	ZoneScopedN("Log::Initialize");

	const std::filesystem::path logDirectory = "Logs";
	if (!std::filesystem::exists(logDirectory)) { std::filesystem::create_directories(logDirectory); }
	// TODO: Separate log files per run?
	const auto logFile = logDirectory / "Luna.log";

	auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
	auto fileSink    = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logFile.string(), true);

	// Set the format for our logs. The two formats are identical, except for the lack of color output for the file sink.
	// e.g. [13:33:12] Luna-I: [Engine] Initializing Luna Engine.
	consoleSink->set_pattern("%^[%T] %n-%L: %v%$");
	fileSink->set_pattern("[%T] %n-%L: %v");

	_mainLogger = std::make_shared<spdlog::logger>("Luna", spdlog::sinks_init_list{consoleSink, fileSink});
	spdlog::register_logger(_mainLogger);

	// Default to Info level logging.
	SetLevel(Level::Info);

	return bool(_mainLogger);
}

void Log::Shutdown() {
	ZoneScopedN("Log::Shutdown");

	_mainLogger->flush();
	_mainLogger.reset();
	spdlog::drop_all();
}
}  // namespace Luna
