#pragma once

#include <Luna/Core/Module.hpp>
#include <Luna/Time/Time.hpp>
#include <Luna/Utility/NonCopyable.hpp>
#include <map>

namespace Luna {
class Engine final : NonCopyable {
 public:
	Engine();
	~Engine() noexcept;

	static Engine* Get() {
		return _instance;
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

	void SetFPSLimit(uint32_t limit);
	void SetUPSLimit(uint32_t limit);

 private:
	static Engine* _instance;

	std::multimap<Module::StageIndex, std::unique_ptr<Module>> _modules;
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
