#pragma once

#include <cstdint>

namespace Luna {
int64_t GetCurrentTimeNanoseconds();

class Timer {
 public:
	void Start();
	double End() const;

 private:
	int64_t _t = 0;
};

class FrameTimer {
 public:
	FrameTimer();

	double GetElapsed() const;
	double GetFrameTime() const;

	void EnterIdle();
	double Frame();
	double Frame(double frameTime);
	void LeaveIdle();
	void Reset();

 private:
	int64_t GetTime() const;

	int64_t _start      = 0;
	int64_t _last       = 0;
	int64_t _lastPeriod = 0;
	int64_t _idleStart  = 0;
	int64_t _idleTime   = 0;
};
}  // namespace Luna
