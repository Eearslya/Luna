#include <Luna/Utility/Timer.hpp>

#ifdef _WIN32
#	define WIN32_LEAN_AND_MEAN
#	define NOMINMAX
#	include <Windows.h>
#else
#	include <ctime>
#endif

namespace Luna {
#ifdef _WIN32
static struct QPCFrequency {
	QPCFrequency() {
		LARGE_INTEGER frequency;
		::QueryPerformanceFrequency(&frequency);
		InverseFrequency = 1e9 / double(frequency.QuadPart);
	}

	double InverseFrequency = 0.0;
} StaticQPCFrequency;
#endif

int64_t GetCurrentTimeNanoseconds() {
#ifdef _WIN32
	LARGE_INTEGER li;
	if (!::QueryPerformanceCounter(&li)) { return 0; }

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
}  // namespace Luna
