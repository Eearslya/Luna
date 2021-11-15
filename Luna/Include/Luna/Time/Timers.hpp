#pragma once

#include <Luna/Core/Module.hpp>
#include <Luna/Time/Time.hpp>
#include <Luna/Utility/Delegate.hpp>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>

namespace Luna {
class Timer {
	friend class Timers;

 public:
	Timer(const Time& interval, const std::optional<uint32_t>& repeat = std::nullopt)
			: _interval(interval), _next(Time::Now() + _interval), _repeat(repeat) {}

	const Time& GetInterval() const {
		return _interval;
	}
	const std::optional<uint32_t>& GetRepeat() const {
		return _repeat;
	}
	bool IsDestroyed() const {
		return _destroyed;
	}

	void Destroy() {
		_destroyed = true;
	}

	Delegate<void()>& OnTick() {
		return _onTick;
	}

 private:
	std::atomic_bool _destroyed = false;
	Time _interval;
	Time _next;
	Delegate<void()> _onTick;
	std::optional<uint32_t> _repeat;
};

class Timers : public Module::Registrar<Timers> {
	static inline const bool Registered = Register("Timers", Stage::Post);

 public:
	Timers();
	~Timers() noexcept;

	void Update() override;

	template <typename... Args>
	Timer* Once(const Time& delay, std::function<void()>&& function, Args... args) {
		std::lock_guard<std::mutex> lock(_timerMutex);

		auto& timer = _timers.emplace_back(new Timer(delay, 1));
		timer->OnTick().Add(std::move(function), args...);
		_timersDirty = true;
		_timerCondition.notify_all();

		return timer.get();
	}

	template <typename... Args>
	Timer* Every(const Time& interval, std::function<void()>&& function, Args... args) {
		std::lock_guard<std::mutex> lock(_timerMutex);

		auto& timer = _timers.emplace_back(new Timer(interval));
		timer->OnTick().Add(std::move(function), args...);
		_timersDirty = true;
		_timerCondition.notify_all();

		return timer.get();
	}

	template <typename... Args>
	Timer* Repeat(const Time& interval, uint32_t repeat, std::function<void()>&& function, Args... args) {
		std::lock_guard<std::mutex> lock(_timerMutex);

		auto& timer = _timers.emplace_back(new Timer(interval, repeat));
		timer->OnTick().Add(std::move(function), args...);
		_timersDirty = true;
		_timerCondition.notify_all();

		return timer.get();
	}

 private:
	void TimerThread();

	std::atomic_bool _stop = false;
	std::condition_variable _timerCondition;
	std::mutex _timerMutex;
	std::vector<std::unique_ptr<Timer>> _timers;
	bool _timersDirty = false;
	std::thread _workerThread;
};
}  // namespace Luna