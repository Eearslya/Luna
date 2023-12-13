#pragma once

#include <Luna/Common.hpp>

namespace Luna {
using namespace std::chrono_literals;

class Time {
 public:
	Time() = default;
	template <typename Rep, typename Period>
	constexpr Time(const std::chrono::duration<Rep, Period>& duration)
			: _value(std::chrono::duration_cast<std::chrono::microseconds>(duration).count()) {}

	[[nodiscard]] static Time Now() {
		static const auto epoch = std::chrono::high_resolution_clock::now();

		return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - epoch);
	}
	template <typename Rep = float>
	[[nodiscard]] static constexpr Time Hours(const Rep hours) {
		return Minutes(hours * static_cast<Rep>(60));
	}
	template <typename Rep = float>
	[[nodiscard]] static constexpr Time Minutes(const Rep minutes) {
		return Seconds(minutes * static_cast<Rep>(60));
	}
	template <typename Rep = float>
	[[nodiscard]] static constexpr Time Seconds(const Rep seconds) {
		return Time(std::chrono::duration<Rep>(seconds));
	}
	template <typename Rep = int32_t>
	[[nodiscard]] static constexpr Time Milliseconds(const Rep milliseconds) {
		return Time(std::chrono::duration<Rep, std::milli>(milliseconds));
	}
	template <typename Rep = int64_t>
	[[nodiscard]] static constexpr Time Microseconds(const Rep microseconds) {
		return Time(std::chrono::duration<Rep, std::micro>(microseconds));
	}

	template <typename T = float>
	[[nodiscard]] constexpr T AsSeconds() const {
		return static_cast<T>(_value.count()) / static_cast<T>(1'000'000);
	}
	template <typename T = int32_t>
	[[nodiscard]] constexpr T AsMilliseconds() const {
		return static_cast<T>(_value.count()) / static_cast<T>(1'000);
	}
	template <typename T = int64_t>
	[[nodiscard]] constexpr T AsMicroseconds() const {
		return static_cast<T>(_value.count());
	}

	template <typename Rep, typename Period>
	[[nodiscard]] constexpr operator std::chrono::duration<Rep, Period>() const {
		return std::chrono::duration_cast<std::chrono::duration<Rep, Period>>(_value);
	}

	[[nodiscard]] constexpr auto operator<=>(const Time& other) const {
		return _value <=> other._value;
	}

	[[nodiscard]] friend constexpr Time operator+(const Time& a, const Time& b) {
		return a._value + b._value;
	}
	[[nodiscard]] friend constexpr Time operator-(const Time& a, const Time& b) {
		return a._value - b._value;
	}
	[[nodiscard]] friend constexpr Time operator*(const Time& a, float b) {
		return a._value * b;
	}
	[[nodiscard]] friend constexpr Time operator*(const Time& a, int64_t b) {
		return a._value * b;
	}
	[[nodiscard]] friend constexpr Time operator*(float a, const Time& b) {
		return a * b._value;
	}
	[[nodiscard]] friend constexpr Time operator*(int64_t a, const Time& b) {
		return a * b._value;
	}
	[[nodiscard]] friend constexpr Time operator/(const Time& a, float b) {
		return a._value / b;
	}
	[[nodiscard]] friend constexpr Time operator/(const Time& a, int64_t b) {
		return a._value / b;
	}
	[[nodiscard]] friend constexpr double operator/(const Time& a, const Time& b) {
		return static_cast<double>(a._value.count()) / static_cast<double>(b._value.count());
	}

 private:
	std::chrono::microseconds _value = {};
};

class ElapsedTime {
 public:
	[[nodiscard]] const Time& Get() const {
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
}  // namespace Luna
