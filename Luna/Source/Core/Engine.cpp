#include <Luna/Core/App.hpp>
#include <Luna/Core/Engine.hpp>
#include <Luna/Core/Log.hpp>
#include <Tracy.hpp>
#include <cstdlib>

namespace Luna {
Engine* Engine::_instance = nullptr;

Engine::Engine(const char* argv0) : _argv0(argv0) {
	ZoneScopedN("Engine::Engine()");

	if (_instance != nullptr) { throw std::runtime_error("Cannot initialize Luna::Engine more than once!"); }
	_instance = this;

#ifdef LUNA_DEBUG
	Log::SetLevel(Log::Level::Trace);
#endif

	Log::Info("Engine", "Initializing Luna Engine.");

	std::vector<TypeID> createdModules;
	uint8_t retryCount = 64;
	while (true) {
		bool postponed = false;

		for (const auto& [moduleID, moduleInfo] : Module::Registry()) {
			if (std::find(createdModules.begin(), createdModules.end(), moduleID) != createdModules.end()) { continue; }

			bool thisPostponed = false;
			for (const auto& dependencyID : moduleInfo.Dependencies) {
				if (std::find(createdModules.begin(), createdModules.end(), dependencyID) == createdModules.end()) {
					thisPostponed = true;
					break;
				}
			}

			if (thisPostponed) {
				postponed = true;
				continue;
			}

			Log::Debug("Engine", "Initializing Engine module '{}'.", moduleInfo.Name);
			auto&& module = moduleInfo.Create();
			_moduleMap.emplace(Module::StageIndex(moduleInfo.Stage, moduleID), module.get());
			_modules.push_back(std::move(module));
			createdModules.emplace_back(moduleID);
		}

		if (postponed) {
			if (--retryCount == 0) {
				Log::Fatal("Engine",
				           "Failed to initialize Engine modules. A dependency is missing or a circular dependency is present.");
				throw std::runtime_error("Failed to initialize Engine modules!");
			}
		} else {
			break;
		}
	}

	Log::Debug("Engine", "All engine modules initialized.");

	SetFPSLimit(_fpsLimit);
	SetUPSLimit(_upsLimit);
}

Engine::~Engine() noexcept {
	Log::Info("Engine", "Shutting down Luna Engine.");

	for (auto modIt = _modules.rbegin(); modIt != _modules.rend(); ++modIt) { modIt->reset(); }
	_modules.clear();
	_moduleMap.clear();
	Module::Registry().clear();
	_instance = nullptr;
}

int Engine::Run() {
	const auto UpdateStage = [this](Module::Stage stage) {
		for (auto& [stageIndex, module] : _moduleMap) {
			if (stageIndex.first == stage) { module->Update(); }
		}
	};

	_running = true;
	while (_running) {
		UpdateStage(Module::Stage::Always);

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
				{
					ZoneScopedN("Update: Pre");
					UpdateStage(Module::Stage::Pre);
				}
				{
					ZoneScopedN("Update: Normal");
					UpdateStage(Module::Stage::Normal);
				}
				{
					ZoneScopedN("Update: Post");
					UpdateStage(Module::Stage::Post);
				}
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
				UpdateStage(Module::Stage::Render);
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
