#include <Luna/Utility/Timer.hpp>

#ifdef _WIN32
#	define WIN32_LEAN_AND_MEAN
#	include <Windows.h>
#else
#	include <ctime>
#endif

namespace Luna {
#ifdef _WIN32
struct QPCFrequency {
	QPCFrequency() {
		LARGE_INTEGER frequency;
		QueryPerformanceFrequency(&frequency);
		InverseFrequency = 1e9 / double(frequency.QuadPart);
	}

	double InverseFrequency = 0.0;
} static StaticQPCFrequency;
#endif

int64_t GetCurrentTimeNanoseconds() {
#ifdef _WIN32
	LARGE_INTEGER li;
	if (!QueryPerformanceCounter(&li)) { return 0; }

	return int64_t(double(li.QuadPart) * StaticQPCFrequency.InverseFrequency);
#else
	struct timespec ts      = {};
	constexpr auto timeBase = CLOCK_MONOTONIC_RAW;
	if (clock_gettime(timebase, &ts) < 0) { return 0; }

	return ts.tv_sec * 1000000000ll + ts.tv_nsec;
#endif
}

void Timer::Start() {
	_t = GetCurrentTimeNanoseconds();
}

double Timer::End() const {
	const auto now = GetCurrentTimeNanoseconds();
	return double(now - _t) * 1e-9;
}

FrameTimer::FrameTimer() {
	Reset();
}

double FrameTimer::GetElapsed() const {
	return double(_last - _start) * 1e-9;
}

double FrameTimer::GetFrameTime() const {
	return double(_lastPeriod) * 1e-9;
}

void FrameTimer::EnterIdle() {
	_idleStart = GetTime();
}

double FrameTimer::Frame() {
	const auto newTime = GetTime() - _idleTime;
	_lastPeriod        = newTime - _last;
	_last              = newTime;

	return double(_lastPeriod) * 1e-9;
}

double FrameTimer::Frame(double frameTime) {
	_lastPeriod = int64_t(frameTime * 1e9);
	_last += _lastPeriod;

	return frameTime;
}

void FrameTimer::LeaveIdle() {
	const auto idleEnd = GetTime();
	_idleTime += idleEnd - _idleStart;
}

void FrameTimer::Reset() {
	_start      = GetTime();
	_last       = _start;
	_lastPeriod = 0;
}

int64_t FrameTimer::GetTime() const {
	return GetCurrentTimeNanoseconds();
}
}  // namespace Luna
