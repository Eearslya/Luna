#include <Luna/Core/Log.hpp>
#include <Luna/Time/Timers.hpp>

namespace Luna {
Timers::Timers() {
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

void Timers::Update() {}

void Timers::TimerThread() {
	std::unique_lock<std::mutex> lock(_timerMutex);

	Log::Info("Timers thread started.");

	while (!_stop) {
		if (_timers.empty()) {
			_timerCondition.wait(lock, [this]() { return _timersDirty; });
		} else {
			_timersDirty = false;

			std::sort(_timers.begin(), _timers.end(), [](const auto& a, const auto& b) { return a->_next < b->_next; });

			auto& timer    = _timers.front();
			const auto now = Time::Now();

			if (now >= timer->_next) {
				lock.unlock();
				timer->_onTick();
				lock.lock();

				timer->_next += timer->_interval;

				if (timer->_repeat) {
					if (--*timer->_repeat == 0) {
						_timers.erase(std::remove(_timers.begin(), _timers.end(), timer), _timers.end());
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
