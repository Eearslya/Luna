#include <Luna/Core/Core.hpp>
#include <Luna/Core/Engine.hpp>
#include <Luna/Core/Log.hpp>
#include <Luna/Core/WindowManager.hpp>
#include <Luna/Graphics/GraphicsManager.hpp>

namespace Luna {
Engine* Engine::_instance = nullptr;

Engine* Engine::Get() {
	return _instance;
}

Engine::Engine() {
	if (_instance != nullptr) { throw std::runtime_error("Luna::Engine can only be initialized once!"); }
	_instance = this;

#ifdef LUNA_DEBUG
	Log::SetLevel(Log::Level::Trace);
#endif

	Log::Info("Engine", "Luna Engine initializing.");

	_windowManager   = std::unique_ptr<WindowManager>(new WindowManager());
	_graphicsManager = std::unique_ptr<Graphics::GraphicsManager>(new Graphics::GraphicsManager());
}

Engine::~Engine() noexcept {
	Log::Info("Engine", "Luna Engine shutting down.");

	_graphicsManager.reset();
	_windowManager.reset();
	_instance = nullptr;
}

void Engine::RequestShutdown() {
	if (_running) {
		Log::Debug("Engine", "Engine shutdown requested.");
		_running = false;
	}
}

int Engine::Run() {
	Log::Debug("Engine", "Starting main Engine loop.");

	_running = true;
	while (_running) {
		_windowManager->Update();

		_graphicsManager->Render();
	}

	return 0;
}
}  // namespace Luna
