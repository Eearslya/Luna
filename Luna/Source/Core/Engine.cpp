#include <Luna/Core/Engine.hpp>
#include <Luna/Core/Log.hpp>
#include <cstdlib>

namespace Luna {
Engine* Engine::_instance = nullptr;

Engine::Engine() {
	_instance = this;

	Log::Info("Initializing Luna engine.");

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

			Log::Debug("Initializing Engine module '{}'.", moduleInfo.Name);
			auto&& module = moduleInfo.Create();
			_modules.emplace(Module::StageIndex(moduleInfo.Stage, moduleID), std::move(module));
			createdModules.emplace_back(moduleID);
		}

		if (postponed) {
			if (--retryCount == 0) {
				Log::Fatal("Failed to initialize Engine modules. A dependency is missing or a circular dependency is present.");
				throw std::runtime_error("Failed to initialize Engine modules!");
			}
		} else {
			break;
		}
	}

	Log::Debug("All engine modules initialized.");
}

Engine::~Engine() noexcept {
	Module::Registry().clear();
	_instance = nullptr;
}

int Engine::Run() {
	return EXIT_SUCCESS;
}
}  // namespace Luna
