#pragma once

#include <Luna/Core/Module.hpp>
#include <Luna/Time/Time.hpp>
#include <Luna/Utility/NonCopyable.hpp>
#include <map>
#include <string>

namespace Luna {
class App;

class Engine final : NonCopyable {
 public:
	Engine(const char* argv0);
	~Engine() noexcept;

	static Engine* Get() {
		return _instance;
	}

	App* GetApp() {
		return _app;
	}
	const std::string& GetArgv0() const {
		return _argv0;
	}
	Time GetFrameDelta() const {
		return _frameDelta.Get();
	}
	uint32_t GetFPS() const {
		return _fps.Get();
	}
	uint32_t GetFPSLimit() const {
		return _fpsLimit;
	}
	Time GetUpdateDelta() const {
		return _updateDelta.Get();
	}
	uint32_t GetUPS() const {
		return _ups.Get();
	}
	uint32_t GetUPSLimit() const {
		return _upsLimit;
	}

	int Run();
	void Shutdown();

	void SetApp(App* app);
	void SetFPSLimit(uint32_t limit);
	void SetUPSLimit(uint32_t limit);

 private:
	static Engine* _instance;

	App* _app = nullptr;
	std::string _argv0;
	std::multimap<Module::StageIndex, Module*> _moduleMap;
	std::vector<std::unique_ptr<Module>> _modules;
	bool _running = false;

	ElapsedTime _frameDelta;
	IntervalCounter _frameLimiter;
	UpdatesPerSecond _fps;
	uint32_t _fpsLimit = 60;

	ElapsedTime _updateDelta;
	IntervalCounter _updateLimiter;
	UpdatesPerSecond _ups;
	uint32_t _upsLimit = 100;
};
}  // namespace Luna
