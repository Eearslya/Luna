#pragma once

#include <Luna/Time/Time.hpp>
#include <Luna/Utility/Delegate.hpp>
#include <Luna/Utility/ObjectPool.hpp>
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

	Delegate<void()> OnTick;

 private:
	std::atomic_bool _destroyed = false;
	Time _interval;
	Time _next;
	std::optional<uint32_t> _repeat;
};

class Timers {
 public:
	Timers();
	~Timers() noexcept;

	static Timers* Get() {
		return _instance;
	}

	template <typename... Args>
	Timer* Once(const Time& delay, std::function<void()>&& function, Args... args) {
		std::lock_guard<std::mutex> lock(_timerMutex);

		Timer* timer = _timerPool.Allocate(delay, 1);
		timer->OnTick.Add(std::move(function), args...);
		_activeTimers.push_back(timer);
		_timersDirty = true;
		_timerCondition.notify_all();

		return timer;
	}

	template <typename... Args>
	Timer* Every(const Time& interval, std::function<void()>&& function, Args... args) {
		std::lock_guard<std::mutex> lock(_timerMutex);

		Timer* timer = _timerPool.Allocate(interval);
		timer->OnTick.Add(std::move(function), args...);
		_activeTimers.push_back(timer);
		_timersDirty = true;
		_timerCondition.notify_all();

		return timer;
	}

	template <typename... Args>
	Timer* Repeat(const Time& interval, uint32_t repeat, std::function<void()>&& function, Args... args) {
		std::lock_guard<std::mutex> lock(_timerMutex);

		Timer* timer = _timerPool.Allocate(interval, repeat);
		timer->OnTick.Add(std::move(function), args...);
		_activeTimers.push_back(timer);
		_timersDirty = true;
		_timerCondition.notify_all();

		return timer;
	}

 private:
	static Timers* _instance;

	void TimerThread();

	ObjectPool<Timer> _timerPool;

	std::atomic_bool _stop = false;
	std::condition_variable _timerCondition;
	std::mutex _timerMutex;
	std::vector<Timer*> _activeTimers;
	bool _timersDirty = false;
	std::thread _workerThread;
};
}  // namespace Luna
