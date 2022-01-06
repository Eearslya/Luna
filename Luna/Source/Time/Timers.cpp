#include <Luna/Core/Log.hpp>
#include <Luna/Time/Timers.hpp>
#include <Tracy.hpp>

namespace Luna {
Timers::Timers() {
	ZoneScopedN("Timers::Timers()");

	std::unique_lock<std::mutex> lock(_timerMutex);

	_workerThread = std::thread(std::bind(&Timers::TimerThread, this));
}

Timers::~Timers() noexcept {
	{
		std::unique_lock<std::mutex> lock(_timerMutex);
		_stop        = true;
		_timersDirty = true;
		_timerCondition.notify_all();
	}

	_workerThread.join();
}

void Timers::TimerThread() {
	std::unique_lock<std::mutex> lock(_timerMutex);

	Log::Info("Timers thread started.");

	tracy::SetThreadName("Timers Thread");

	while (!_stop) {
		if (_activeTimers.empty()) {
			_timerCondition.wait(lock, [this]() { return _timersDirty; });
		} else {
			_timersDirty = false;

			std::sort(
				_activeTimers.begin(), _activeTimers.end(), [](const auto& a, const auto& b) { return a->_next < b->_next; });

			Timer* timer   = _activeTimers.front();
			const auto now = Time::Now();

			if (now >= timer->_next) {
				lock.unlock();
				timer->_onTick();
				lock.lock();

				timer->_next += timer->_interval;

				if (timer->_repeat) {
					if (--*timer->_repeat == 0) {
						_activeTimers.erase(std::remove(_activeTimers.begin(), _activeTimers.end(), timer), _activeTimers.end());
						_timerPool.Free(timer);
					}
				}
			} else {
				const std::chrono::microseconds nextTimer(timer->_next - now);
				_timerCondition.wait_for(lock, nextTimer, [this]() { return _timersDirty; });
			}
		}
	}
}
}  // namespace Luna
