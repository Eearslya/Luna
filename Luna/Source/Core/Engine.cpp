#include <Luna/Core/App.hpp>
#include <Luna/Core/Engine.hpp>
#include <Luna/Core/Log.hpp>
#include <Luna/Devices/Keyboard.hpp>
#include <Luna/Devices/Mouse.hpp>
#include <Luna/Devices/Window.hpp>
#include <Luna/Filesystem/Filesystem.hpp>
#include <Luna/Graphics/Graphics.hpp>
#include <Luna/Threading/Threading.hpp>
#include <Luna/Time/Timers.hpp>
#include <Tracy.hpp>
#include <cstdlib>

namespace Luna {
Engine* Engine::_instance = nullptr;

Engine::Engine(const char* argv0) : _argv0(argv0) {
	ZoneScopedN("Engine::Engine()");

	if (_instance != nullptr) { throw std::runtime_error("Cannot initialize Luna::Engine more than once!"); }
	_instance = this;

	Log::Initialize();
#ifdef LUNA_DEBUG
	Log::SetLevel(Log::Level::Trace);
#endif

	Log::Info("Engine", "Initializing Luna Engine.");

	_modFilesystem = std::make_unique<Filesystem>();
	_modThreading  = std::make_unique<Threading>();
	_modTimers     = std::make_unique<Timers>();
	_modWindow     = std::make_unique<Window>();
	_modKeyboard   = std::make_unique<Keyboard>();
	_modMouse      = std::make_unique<Mouse>();
	_modGraphics   = std::make_unique<Graphics>();

	SetFPSLimit(_fpsLimit);
	SetUPSLimit(_upsLimit);
}

Engine::~Engine() noexcept {
	Log::Info("Engine", "Shutting down Luna Engine.");

	_modGraphics.reset();
	_modMouse.reset();
	_modKeyboard.reset();
	_modWindow.reset();
	_modTimers.reset();
	_modThreading.reset();
	_modFilesystem.reset();

	Log::Shutdown();

	_instance = nullptr;
}

int Engine::Run() {
	_running = true;
	while (_running) {
		_updateLimiter.Update();
		if (_updateLimiter.Get() > 0) {
			ZoneScopedN("Update");

			_ups.Update();
			_updateDelta.Update();

			if (_app) {
				if (!_app->_started) {
					_app->Start();
					_app->_started = true;
				}

				try {
					ZoneScopedN("Update: App");
					_app->Update();
				} catch (const std::exception& e) {
					Log::Fatal("Engine", "Caught fatal error when updating application: {}", e.what());
					_running = false;
					break;
				}
			}

			try {
				ZoneScopedN("Update: Modules");
				_modWindow->Update();
				_modMouse->Update();
			} catch (const std::exception& e) {
				Log::Fatal("Engine", "Caught fatal error when updating engine modules: {}", e.what());
				_running = false;
				break;
			}
		}

		_frameLimiter.Update();
		if (_frameLimiter.Get() > 0) {
			_fps.Update();
			_frameDelta.Update();
			try {
				ZoneScopedN("Update: Render");
				_modGraphics->Update();
			} catch (const std::exception& e) {
				Log::Fatal("Engine", "Caught fatal error when rendering: {}", e.what());
				_running = false;
			}
		}
	}

	if (_app) {
		_app->Stop();
		_app->_started = false;
	}

	return EXIT_SUCCESS;
}

void Engine::Shutdown() {
	_running = false;
}

void Engine::SetActiveProject(const Ref<Project>& project) {
	Log::Info("Engine", "Switching to active project: '{}'", project->Name);

	_activeProject = project;
}

void Engine::SetApp(App* app) {
	if (_app) {
		_app->Stop();
		_app->_started = false;
	}
	_app = app;
}

void Engine::SetFPSLimit(uint32_t limit) {
	_fpsLimit = limit;
	_frameLimiter.SetInterval(Time::Seconds(1.0f / limit));
}

void Engine::SetUPSLimit(uint32_t limit) {
	_upsLimit = limit;
	_updateLimiter.SetInterval(Time::Seconds(1.0f / limit));
}
}  // namespace Luna
