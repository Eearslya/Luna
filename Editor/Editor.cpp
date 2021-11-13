#include <Luna.hpp>
#include <memory>

using namespace Luna;

int main(int argc, const char** argv) {
#ifdef LUNA_DEBUG
	Log::SetLevel(spdlog::level::trace);
#endif

	std::unique_ptr<Engine> engine = std::make_unique<Engine>();

	return engine->Run();
}
