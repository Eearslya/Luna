#include <Luna/Core/Engine.hpp>
#include <Luna/Core/Log.hpp>

namespace Luna {
bool Engine::Initialize() {
	if (!Log::Initialize()) { return false; }
	Log::SetLevel(Log::Level::Trace);
	Log::Info("Luna", "Luna Engine initializing...");

	return true;
}

int Engine::Run() {
	return 0;
}

void Engine::Shutdown() {
	Log::Shutdown();
}
}  // namespace Luna
