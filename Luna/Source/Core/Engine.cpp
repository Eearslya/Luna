#include <Luna/Core/Engine.hpp>
#include <Luna/Core/Log.hpp>
#include <cstdlib>

namespace Luna {
Engine* Engine::_instance = nullptr;

Engine::Engine() {
	_instance = this;

	Log::Info("Initializing Luna engine.");
}

Engine::~Engine() noexcept {}

int Engine::Run() {
	return EXIT_SUCCESS;
}
}  // namespace Luna
