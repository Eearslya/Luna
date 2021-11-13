#include <Luna.hpp>
#include <memory>

using namespace Luna;

int main(int argc, const char** argv) {
	Log::SetLevel(spdlog::level::trace);

	std::unique_ptr<Engine> engine = std::make_unique<Engine>();

	return engine->Run();
}
