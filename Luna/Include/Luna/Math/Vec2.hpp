#pragma once

#include <cmath>
#include <cstdint>
#include <iostream>

namespace Luna {
template <typename T>
class Vec2 {
 public:
	// Default initialize to 0.
	constexpr Vec2() = default;

	// Initialize x and y with the same value.
	template <typename K>
	constexpr Vec2(const K& v) : x(static_cast<T>(v)), y(static_cast<T>(v)) {}

	// Initialize x and y with separate values.
	template <typename J, typename K>
	constexpr Vec2(const J& x, const K& y) : x(static_cast<T>(x)), y(static_cast<T>(y)) {}

	// Copy from an existing Vec2.
	template <typename K>
	constexpr Vec2(const Vec2<K>& other) : x(static_cast<T>(other.x)), y(static_cast<T>(other.y)) {}

	// Math operations.
	template <typename K>
	constexpr auto Add(const Vec2<K>& other) const {
		return Vec2<decltype(x + other.x)>(x + other.x, y + other.y);
	}
	template <typename K>
	constexpr auto Subtract(const Vec2<K>& other) const {
		return Vec2<decltype(x - other.x)>(x - other.x, y - other.y);
	}
	template <typename K>
	constexpr auto Multiply(const Vec2<K>& other) const {
		return Vec2<decltype(x * other.x)>(x * other.x, y * other.y);
	}
	template <typename K>
	constexpr auto Divide(const Vec2<K>& other) const {
		return Vec2<decltype(x / other.x)>(x / other.x, y / other.y);
	}

	// Vector operations.
	template <typename K>
	constexpr auto Distance(const Vec2<K>& other) const {
		return std::sqrt(DistanceSquared(other));
	}
	template <typename K>
	constexpr auto DistanceSquared(const Vec2<K>& other) const {
		const auto dx = x - other.x;
		const auto dy = y - other.y;

		return dx * dx + dy * dy;
	}
	template <typename K>
	constexpr auto DistanceVector(const Vec2<K>& other) const {
		return (*this - other) * (*this - other);
	}
	constexpr auto Length() const {
		return std::sqrt(LengthSquared());
	}
	constexpr auto LengthSquared() const {
		return x * x + y * y;
	}
	constexpr auto Max() const {
		return std::max(x, y);
	}
	template <typename K>
	constexpr auto Max(const Vec2<K>& other) const {
		using V = decltype(x + other.x);

		return Vec2<V>(std::max<V>(x, other.x), std::max<V>(y, other.y));
	}
	constexpr auto Min() const {
		return std::min(x, y);
	}
	template <typename K>
	constexpr auto Min(const Vec2<K>& other) const {
		using V = decltype(x + other.x);

		return Vec2<V>(std::min<V>(x, other.x), std::min<V>(y, other.y));
	}
	constexpr auto Normalized() const {
		const auto l = Length();
		if (l == 0) { throw std::invalid_argument("Cannot normalize a zero vector!"); }

		return *this / l;
	}

	// Equality operators.
	template <typename K>
	constexpr bool operator==(const Vec2<T>& other) const {
		return x == other.x && y == other.y;
	}
	template <typename K>
	constexpr bool operator!=(const Vec2<T>& other) const {
		return x != other.x || y != other.y;
	}

	// Unary operators.
	template <typename U = T>
	constexpr std::enable_if_t<std::is_signed_v<U>, Vec2> operator-() const {
		return {-x, -y};
	}
	template <typename U = T>
	constexpr std::enable_if_t<std::is_integral_v<U>, Vec2> operator~() const {
		return {~x, ~y};
	}

	// Array operators.
	const T& operator[](size_t index) const {
		if (index < 0 || index > 2) { throw std::range_error("Out-of-range access for Vec2!"); }

		return data[index];
	}
	T& operator[](size_t index) {
		if (index < 0 || index > 2) { throw std::range_error("Out-of-range access for Vec2!"); }

		return data[index];
	}

	// Math operators.
	template <typename A, typename B>
	constexpr auto operator+=(const Vec2<B>& other) {
		return *this = Add(other);
	}
	template <typename K>
	constexpr auto operator+=(const K& other) {
		return *this = Add(Vec2<K>(other));
	}

	template <typename A, typename B>
	constexpr auto operator-=(const Vec2<B>& other) {
		return *this = Subtract(other);
	}
	template <typename K>
	constexpr auto operator-=(const K& other) {
		return *this = Subtract(Vec2<K>(other));
	}

	template <typename A, typename B>
	constexpr auto operator*=(const Vec2<B>& other) {
		return *this = Multiply(other);
	}
	template <typename K>
	constexpr auto operator*=(const K& other) {
		return *this = Multiply(Vec2<K>(other));
	}

	template <typename A, typename B>
	constexpr auto operator/=(const Vec2<B>& other) {
		return *this = Divide(other);
	}
	template <typename K>
	constexpr auto operator/=(const K& other) {
		return *this = Divide(Vec2<K>(other));
	}

	// Prebuilt constants.
	static const Vec2 Zero;
	static const Vec2 One;
	static const Vec2 Infinity;
	static const Vec2 Left;
	static const Vec2 Right;
	static const Vec2 Up;
	static const Vec2 Down;

	union {
		struct {
			T x;
			T y;
		};
		struct {
			T r;
			T g;
		};
		struct {
			T s;
			T t;
		};
		T data[2] = {0};
	};
};

template <typename T>
std::ostream& operator<<(std::ostream& oss, const Vec2<T>& vec) {
	return oss << vec.x << ", " << vec.y;
}

template <typename A, typename B>
constexpr auto operator+(const Vec2<A>& a, const Vec2<B>& b) {
	return a.Add(b);
}
template <typename A, typename B>
constexpr auto operator+(const A& a, const Vec2<B>& b) {
	return Vec2<A>(a).Add(b);
}
template <typename A, typename B>
constexpr auto operator+(const Vec2<A>& a, const B& b) {
	return a.Add(Vec2<B>(b));
}

template <typename A, typename B>
constexpr auto operator-(const Vec2<A>& a, const Vec2<B>& b) {
	return a.Subtract(b);
}
template <typename A, typename B>
constexpr auto operator-(const A& a, const Vec2<B>& b) {
	return Vec2<A>(a).Subtract(b);
}
template <typename A, typename B>
constexpr auto operator-(const Vec2<A>& a, const B& b) {
	return a.Subtract(Vec2<B>(b));
}

template <typename A, typename B>
constexpr auto operator*(const Vec2<A>& a, const Vec2<B>& b) {
	return a.Multiply(b);
}
template <typename A, typename B>
constexpr auto operator*(const A& a, const Vec2<B>& b) {
	return Vec2<A>(a).Multiply(b);
}
template <typename A, typename B>
constexpr auto operator*(const Vec2<A>& a, const B& b) {
	return a.Multiply(Vec2<B>(b));
}

template <typename A, typename B>
constexpr auto operator/(const Vec2<A>& a, const Vec2<B>& b) {
	return a.Divide(b);
}
template <typename A, typename B>
constexpr auto operator/(const A& a, const Vec2<B>& b) {
	return Vec2<A>(a).Divide(b);
}
template <typename A, typename B>
constexpr auto operator/(const Vec2<A>& a, const B& b) {
	return a.Divide(Vec2<B>(b));
}

using Vec2f  = Vec2<float>;
using Vec2d  = Vec2<double>;
using Vec2i  = Vec2<int32_t>;
using Vec2ui = Vec2<uint32_t>;
using Vec2us = Vec2<uint16_t>;
}  // namespace Luna
