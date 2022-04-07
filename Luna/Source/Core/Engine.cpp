#include <Luna/Core/Core.hpp>
#include <Luna/Core/Engine.hpp>
#include <Luna/Core/Log.hpp>

namespace Luna {
Engine::Engine() {
#ifdef LUNA_DEBUG
	Log::SetLevel(Log::Level::Trace);
#endif

	Log::Info("Engine", "Luna Engine initializing.");
}

Engine::~Engine() noexcept {
	Log::Info("Engine", "Luna Engine shutting down.");
}
}  // namespace Luna
