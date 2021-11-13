#pragma once

#include <chrono>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>

namespace Luna {
using namespace std::chrono_literals;

class Time {
 public:
	Time() = default;
	template <typename Rep, typename Period>
	constexpr Time(const std::chrono::duration<Rep, Period>& duration)
			: _value(std::chrono::duration_cast<std::chrono::microseconds>(duration).count()) {}

	static std::string FormatTime(const std::string& format = "%Y-%m-%d %H:%M:%S") {
		const auto now   = std::chrono::system_clock::now();
		const auto timeT = std::chrono::system_clock::to_time_t(now);
		tm localTime     = {};
#ifdef _MSC_VER
		_localtime64_s(&localTime, &timeT);
#elif defined(__APPLE__)
		localtime_r(&timeT, &localTime);
#else
		localtime_s(&timeT, &localTime);
#endif
		std::ostringstream oss;
		oss << std::put_time(&localTime, format.c_str());

		return oss.str();
	}
	static Time Now() {
		static const auto epoch = std::chrono::high_resolution_clock::now();

		return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - epoch);
	}
	template <typename Rep = float>
	static constexpr Time Seconds(const Rep seconds) {
		return Time(std::chrono::duration<Rep>(seconds));
	}
	template <typename Rep = int32_t>
	static constexpr Time Milliseconds(const Rep milliseconds) {
		return Time(std::chrono::duration<Rep, std::milli>(milliseconds));
	}
	template <typename Rep = int64_t>
	static constexpr Time Microseconds(const Rep microseconds) {
		return Time(std::chrono::duration<Rep, std::micro>(microseconds));
	}

	template <typename T = float>
	constexpr T Seconds() const {
		return static_cast<T>(_value.count()) / static_cast<T>(1000000);
	}
	template <typename T = int32_t>
	constexpr T Milliseconds() const {
		return static_cast<T>(_value.count()) / static_cast<T>(1000);
	}
	template <typename T = int64_t>
	constexpr T Microseconds() const {
		return static_cast<T>(_value.count());
	}

	template <typename Rep, typename Period>
	constexpr operator std::chrono::duration<Rep, Period>() const {
		return std::chrono::duration_cast<std::chrono::duration<Rep, Period>>(_value);
	}

	constexpr bool operator==(const Time& other) const {
		return _value == other._value;
	}
	constexpr bool operator!=(const Time& other) const {
		return _value != other._value;
	}
	constexpr bool operator<(const Time& other) const {
		return _value < other._value;
	}
	constexpr bool operator<=(const Time& other) const {
		return _value <= other._value;
	}
	constexpr bool operator>(const Time& other) const {
		return _value > other._value;
	}
	constexpr bool operator>=(const Time& other) const {
		return _value >= other._value;
	}

	constexpr Time operator-() const {
		return -_value;
	}

	friend constexpr Time operator+(const Time& a, const Time& b) {
		return a._value + b._value;
	}
	friend constexpr Time operator-(const Time& a, const Time& b) {
		return a._value - b._value;
	}
	friend constexpr Time operator*(const Time& a, float b) {
		return a._value * b;
	}
	friend constexpr Time operator*(const Time& a, int64_t b) {
		return a._value * b;
	}
	friend constexpr Time operator*(float a, const Time& b) {
		return a * b._value;
	}
	friend constexpr Time operator*(int64_t a, const Time& b) {
		return a * b._value;
	}
	friend constexpr Time operator/(const Time& a, float b) {
		return a._value / b;
	}
	friend constexpr Time operator/(const Time& a, int64_t b) {
		return a._value / b;
	}
	friend constexpr double operator/(const Time& a, const Time& b) {
		return static_cast<double>(a._value.count()) / static_cast<double>(b._value.count());
	}

	constexpr Time& operator+=(const Time& other) {
		return *this = *this + other;
	}
	constexpr Time& operator-=(const Time& other) {
		return *this = *this - other;
	}
	constexpr Time& operator*=(float scalar) {
		return *this = *this * scalar;
	}
	constexpr Time& operator*=(int64_t scalar) {
		return *this = *this * scalar;
	}
	constexpr Time& operator/=(float scalar) {
		return *this = *this / scalar;
	}
	constexpr Time& operator/=(int64_t scalar) {
		return *this = *this / scalar;
	}

 private:
	std::chrono::microseconds _value = {};
};

// Record the time taken between subsequent calls of Update().
class ElapsedTime {
 public:
	const Time& Get() const {
		return _delta;
	}

	void Update() {
		_startTime = Time::Now();
		_delta     = _startTime - _lastTime;
		_lastTime  = _startTime;
	}

 private:
	Time _delta;
	Time _lastTime;
	Time _startTime;
};

// Record how many "intervals" have passed
class IntervalCounter {
 public:
	explicit IntervalCounter(const Time& interval = Time::Seconds(-1)) : _startTime(Time::Now()), _interval(interval) {}

	uint32_t Get() const {
		return _value;
	}

	const Time& GetInterval() const {
		return _interval;
	}
	const Time& GetStartTime() const {
		return _startTime;
	}

	void SetInterval(const Time& interval) {
		_interval = interval;
	}
	void SetStartTime(const Time& startTime) {
		_startTime = startTime;
	}

	void Update() {
		const auto now     = Time::Now();
		const auto elapsed = static_cast<uint32_t>(std::floor((now - _startTime) / _interval));
		if (elapsed != 0) { _startTime = now; }
		_value = elapsed;
	}

 private:
	Time _interval;
	Time _startTime;
	uint32_t _value = 0;
};

// Record the number of times Update() has been called within the past second.
class UpdatesPerSecond {
 public:
	uint32_t Get() const {
		return _value;
	}

	void Update() {
		++_updatesThisSecond;

		const auto now = Time::Now();
		if (std::floor(now.Seconds()) > std::floor(_secondStart.Seconds())) {
			_value             = _updatesThisSecond;
			_updatesThisSecond = 0;
			_secondStart       = now;
		}
	}

 private:
	uint32_t _value = 0;
	Time _secondStart;
	uint32_t _updatesThisSecond = 0;
};
}  // namespace Luna
