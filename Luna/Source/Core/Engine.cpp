#include <Luna/Core/Engine.hpp>
#include <cstdlib>

namespace Luna {
Engine::Engine() {}

Engine::~Engine() noexcept {}

int Engine::Run() {
	return EXIT_SUCCESS;
}
}  // namespace Luna
