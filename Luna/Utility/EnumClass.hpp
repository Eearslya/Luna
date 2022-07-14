#pragma once

#include <type_traits>

namespace Luna {
template <typename Enum>
struct EnableBitmaskOperators : std::false_type {};

template <typename Enum>
constexpr inline bool EnableBitmaskOperatorsV = EnableBitmaskOperators<Enum>::value;

template <typename T>
struct Enumerator {
	T Value;

	constexpr Enumerator(const T& value) : Value(value) {}
	constexpr operator bool() const {
		using UnderlyingT = typename std::underlying_type_t<T>;

		return static_cast<UnderlyingT>(Value) != 0;
	}
	constexpr operator T() const {
		return Value;
	}
};

template <typename T>
struct Bitmask {
	using UnderlyingT = typename std::underlying_type_t<T>;

	UnderlyingT Value;

	constexpr Bitmask() : Value(static_cast<UnderlyingT>(0)) {}
	constexpr Bitmask(const T& value) : Value(static_cast<UnderlyingT>(value)) {}
	constexpr Bitmask(const UnderlyingT& value) : Value(value) {}
	constexpr Bitmask(const Enumerator<T>& enumerator) : Value(static_cast<UnderlyingT>(enumerator.Value)) {}

	constexpr operator bool() const {
		return Value != 0;
	}
};

template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && EnableBitmaskOperatorsV<T>, Bitmask<T>> MakeBitmask(
	const T& t) {
	return {t};
}
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && EnableBitmaskOperatorsV<T>, Bitmask<T>> MakeBitmask(
	const std::underlying_type_t<T>& t) {
	return {t};
}

// AND operations.
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && EnableBitmaskOperatorsV<T>, Enumerator<T>> operator&(
	const T& a, const T& b) {
	using UnderlyingT = typename std::underlying_type_t<T>;
	return {static_cast<T>(static_cast<UnderlyingT>(a) & static_cast<UnderlyingT>(b))};
}
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && EnableBitmaskOperatorsV<T>, Enumerator<T>> operator&(
	const Enumerator<T>& a, const Enumerator<T>& b) {
	using UnderlyingT = typename std::underlying_type_t<T>;
	return {static_cast<T>(static_cast<UnderlyingT>(a.Value) & static_cast<UnderlyingT>(b.Value))};
}
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && EnableBitmaskOperatorsV<T>, Bitmask<T>> operator&(
	const Bitmask<T>& a, const Bitmask<T>& b) {
	using UnderlyingT = typename std::underlying_type_t<T>;
	return {static_cast<T>(a.Value & b.Value)};
}
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && EnableBitmaskOperatorsV<T>, Enumerator<T>> operator&(
	const Bitmask<T>& a, const T& b) {
	using UnderlyingT = typename std::underlying_type_t<T>;
	return {static_cast<T>(a.Value & static_cast<UnderlyingT>(b))};
}
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && EnableBitmaskOperatorsV<T>, Enumerator<T>> operator&(
	const T& a, const Bitmask<T>& b) {
	using UnderlyingT = typename std::underlying_type_t<T>;
	return {static_cast<T>(static_cast<UnderlyingT>(a) & b.Value)};
}
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && EnableBitmaskOperatorsV<T>, Enumerator<T>> operator&(
	const Bitmask<T>& a, const Enumerator<T>& b) {
	using UnderlyingT = typename std::underlying_type_t<T>;
	return {static_cast<T>(a.Value & static_cast<UnderlyingT>(b.Value))};
}
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && EnableBitmaskOperatorsV<T>, Enumerator<T>> operator&(
	const Enumerator<T>& a, const Bitmask<T>& b) {
	using UnderlyingT = typename std::underlying_type_t<T>;
	return {static_cast<T>(static_cast<UnderlyingT>(a.Value) & b.Value)};
}
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && EnableBitmaskOperatorsV<T>, Enumerator<T>> operator&(
	const T& a, const Enumerator<T>& b) {
	using UnderlyingT = typename std::underlying_type_t<T>;
	return {static_cast<T>(static_cast<UnderlyingT>(a) & static_cast<UnderlyingT>(b.Value))};
}
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && EnableBitmaskOperatorsV<T>, Enumerator<T>> operator&(
	const Enumerator<T>& a, const T& b) {
	using UnderlyingT = typename std::underlying_type_t<T>;
	return {static_cast<T>(static_cast<UnderlyingT>(a.Value) & static_cast<UnderlyingT>(b))};
}

// OR operations.
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && EnableBitmaskOperatorsV<T>, Bitmask<T>> operator|(const T& a,
                                                                                                           const T& b) {
	using UnderlyingT = typename std::underlying_type_t<T>;
	return {static_cast<T>(static_cast<UnderlyingT>(a) | static_cast<UnderlyingT>(b))};
}
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && EnableBitmaskOperatorsV<T>, Bitmask<T>> operator|(
	const Enumerator<T>& a, const Enumerator<T>& b) {
	using UnderlyingT = typename std::underlying_type_t<T>;
	return {static_cast<T>(static_cast<UnderlyingT>(a.Value) | static_cast<UnderlyingT>(b.Value))};
}
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && EnableBitmaskOperatorsV<T>, Bitmask<T>> operator|(
	const Bitmask<T>& a, const Bitmask<T>& b) {
	using UnderlyingT = typename std::underlying_type_t<T>;
	return {static_cast<T>(a.Value | b.Value)};
}
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && EnableBitmaskOperatorsV<T>, Bitmask<T>> operator|(
	const Bitmask<T>& a, const T& b) {
	using UnderlyingT = typename std::underlying_type_t<T>;
	return {static_cast<T>(a.Value | static_cast<UnderlyingT>(b))};
}
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && EnableBitmaskOperatorsV<T>, Bitmask<T>> operator|(
	const T& a, const Bitmask<T>& b) {
	using UnderlyingT = typename std::underlying_type_t<T>;
	return {static_cast<T>(static_cast<UnderlyingT>(a) | b.Value)};
}
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && EnableBitmaskOperatorsV<T>, Bitmask<T>> operator|(
	const Bitmask<T>& a, const Enumerator<T>& b) {
	using UnderlyingT = typename std::underlying_type_t<T>;
	return {static_cast<T>(a.Value | static_cast<UnderlyingT>(b.Value))};
}
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && EnableBitmaskOperatorsV<T>, Bitmask<T>> operator|(
	const Enumerator<T>& a, const Bitmask<T>& b) {
	using UnderlyingT = typename std::underlying_type_t<T>;
	return {static_cast<T>(static_cast<UnderlyingT>(a.Value) | b.Value)};
}
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && EnableBitmaskOperatorsV<T>, Bitmask<T>> operator|(
	const T& a, const Enumerator<T>& b) {
	using UnderlyingT = typename std::underlying_type_t<T>;
	return {static_cast<T>(static_cast<UnderlyingT>(a) | static_cast<UnderlyingT>(b.Value))};
}
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && EnableBitmaskOperatorsV<T>, Bitmask<T>> operator|(
	const Enumerator<T>& a, const T& b) {
	using UnderlyingT = typename std::underlying_type_t<T>;
	return {static_cast<T>(static_cast<UnderlyingT>(a.Value) | static_cast<UnderlyingT>(b))};
}

// XOR operations.
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && EnableBitmaskOperatorsV<T>, Bitmask<T>> operator^(const T& a,
                                                                                                           const T& b) {
	using UnderlyingT = typename std::underlying_type_t<T>;
	return {static_cast<T>(static_cast<UnderlyingT>(a) ^ static_cast<UnderlyingT>(b))};
}
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && EnableBitmaskOperatorsV<T>, Bitmask<T>> operator^(
	const Enumerator<T>& a, const Enumerator<T>& b) {
	using UnderlyingT = typename std::underlying_type_t<T>;
	return {static_cast<T>(static_cast<UnderlyingT>(a.Value) ^ static_cast<UnderlyingT>(b.Value))};
}
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && EnableBitmaskOperatorsV<T>, Bitmask<T>> operator^(
	const Bitmask<T>& a, const Bitmask<T>& b) {
	using UnderlyingT = typename std::underlying_type_t<T>;
	return {static_cast<T>(a.Value ^ b.Value)};
}
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && EnableBitmaskOperatorsV<T>, Bitmask<T>> operator^(
	const Bitmask<T>& a, const T& b) {
	using UnderlyingT = typename std::underlying_type_t<T>;
	return {static_cast<T>(a.Value ^ static_cast<UnderlyingT>(b))};
}
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && EnableBitmaskOperatorsV<T>, Bitmask<T>> operator^(
	const T& a, const Bitmask<T>& b) {
	using UnderlyingT = typename std::underlying_type_t<T>;
	return {static_cast<T>(static_cast<UnderlyingT>(a) ^ b.Value)};
}
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && EnableBitmaskOperatorsV<T>, Bitmask<T>> operator^(
	const Bitmask<T>& a, const Enumerator<T>& b) {
	using UnderlyingT = typename std::underlying_type_t<T>;
	return {static_cast<T>(a.Value ^ static_cast<UnderlyingT>(b.Value))};
}
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && EnableBitmaskOperatorsV<T>, Bitmask<T>> operator^(
	const Enumerator<T>& a, const Bitmask<T>& b) {
	using UnderlyingT = typename std::underlying_type_t<T>;
	return {static_cast<T>(static_cast<UnderlyingT>(a.Value) ^ b.Value)};
}
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && EnableBitmaskOperatorsV<T>, Bitmask<T>> operator^(
	const T& a, const Enumerator<T>& b) {
	using UnderlyingT = typename std::underlying_type_t<T>;
	return {static_cast<T>(static_cast<UnderlyingT>(a) ^ static_cast<UnderlyingT>(b.Value))};
}
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && EnableBitmaskOperatorsV<T>, Bitmask<T>> operator^(
	const Enumerator<T>& a, const T& b) {
	using UnderlyingT = typename std::underlying_type_t<T>;
	return {static_cast<T>(static_cast<UnderlyingT>(a.Value) ^ static_cast<UnderlyingT>(b))};
}

// NOT operations.
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && EnableBitmaskOperatorsV<T>, Bitmask<T>> operator~(const T& a) {
	using UnderlyingT = typename std::underlying_type_t<T>;
	return {static_cast<T>(~static_cast<UnderlyingT>(a))};
}
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && EnableBitmaskOperatorsV<T>, Bitmask<T>> operator~(
	const Enumerator<T>& a) {
	using UnderlyingT = typename std::underlying_type_t<T>;
	return {static_cast<T>(~static_cast<UnderlyingT>(a.Value))};
}
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && EnableBitmaskOperatorsV<T>, Bitmask<T>> operator~(
	const Bitmask<T>& a) {
	using UnderlyingT = typename std::underlying_type_t<T>;
	return {static_cast<T>(~a.Value)};
}

// AND-Assign operations.
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && EnableBitmaskOperatorsV<T>, Bitmask<T>> operator&=(
	Bitmask<T>& a, const T& b) {
	using UnderlyingT = typename std::underlying_type_t<T>;
	a.Value &= static_cast<UnderlyingT>(b);
	return a;
}
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && EnableBitmaskOperatorsV<T>, Bitmask<T>> operator&=(
	Bitmask<T>& a, const Enumerator<T>& b) {
	using UnderlyingT = typename std::underlying_type_t<T>;
	a.Value &= static_cast<UnderlyingT>(b.Value);
	return a;
}
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && EnableBitmaskOperatorsV<T>, Bitmask<T>> operator&=(
	Bitmask<T>& a, const Bitmask<T>& b) {
	a.Value &= b.Value;
	return a;
}

// OR-Assign operations.
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && EnableBitmaskOperatorsV<T>, Bitmask<T>> operator|=(
	Bitmask<T>& a, const T& b) {
	using UnderlyingT = typename std::underlying_type_t<T>;
	a.Value |= static_cast<UnderlyingT>(b);
	return a;
}
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && EnableBitmaskOperatorsV<T>, Bitmask<T>> operator|=(
	Bitmask<T>& a, const Enumerator<T>& b) {
	using UnderlyingT = typename std::underlying_type_t<T>;
	a.Value |= static_cast<UnderlyingT>(b.Value);
	return a;
}
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && EnableBitmaskOperatorsV<T>, Bitmask<T>> operator|=(
	Bitmask<T>& a, const Bitmask<T>& b) {
	a.Value |= b.Value;
	return a;
}

// XOR-Assign operations.
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && EnableBitmaskOperatorsV<T>, Bitmask<T>> operator^=(
	Bitmask<T>& a, const T& b) {
	using UnderlyingT = typename std::underlying_type_t<T>;
	a.Value ^= static_cast<UnderlyingT>(b);
	return a;
}
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && EnableBitmaskOperatorsV<T>, Bitmask<T>> operator^=(
	Bitmask<T>& a, const Enumerator<T>& b) {
	using UnderlyingT = typename std::underlying_type_t<T>;
	a.Value ^= static_cast<UnderlyingT>(b.Value);
	return a;
}
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && EnableBitmaskOperatorsV<T>, Bitmask<T>> operator^=(
	Bitmask<T>& a, const Bitmask<T>& b) {
	a.Value ^= b.Value;
	return a;
}

// Equality operations.
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && EnableBitmaskOperatorsV<T>, bool> operator==(const T& a,
                                                                                                      const T& b) {
	return a == b;
}
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && EnableBitmaskOperatorsV<T>, bool> operator==(
	const Enumerator<T>& a, const Enumerator<T>& b) {
	return a.Value == b.Value;
}
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && EnableBitmaskOperatorsV<T>, bool> operator==(
	const Bitmask<T>& a, const Bitmask<T>& b) {
	return a.Value == b.Value;
}
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && EnableBitmaskOperatorsV<T>, bool> operator==(
	const Enumerator<T>& a, const T& b) {
	return a.Value == b;
}
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && EnableBitmaskOperatorsV<T>, bool> operator==(
	const T& a, const Enumerator<T>& b) {
	return a == b.Value;
}
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && EnableBitmaskOperatorsV<T>, bool> operator==(
	const Bitmask<T>& a, const T& b) {
	static_assert(!std::is_same_v<typename Bitmask<T>::UnderlyingT, std::underlying_type_t<T>>,
	              "A bitmask cannot be compared to an enumerator. Use & first.");
	return false;
}
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && EnableBitmaskOperatorsV<T>, bool> operator==(
	const T& a, const Bitmask<T>& b) {
	static_assert(!std::is_same_v<typename Bitmask<T>::UnderlyingT, std::underlying_type_t<T>>,
	              "A bitmask cannot be compared to an enumerator. Use & first.");
	return false;
}
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && EnableBitmaskOperatorsV<T>, bool> operator==(
	const Bitmask<T>& a, const Enumerator<T>& b) {
	static_assert(!std::is_same_v<typename Bitmask<T>::UnderlyingT, std::underlying_type_t<T>>,
	              "A bitmask cannot be compared to an enumerator. Use & first.");
	return false;
}
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && EnableBitmaskOperatorsV<T>, bool> operator==(
	const Enumerator<T>& a, const Bitmask<T>& b) {
	static_assert(!std::is_same_v<typename Bitmask<T>::UnderlyingT, std::underlying_type_t<T>>,
	              "A bitmask cannot be compared to an enumerator. Use & first.");
	return false;
}

// Inequality operations.
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && EnableBitmaskOperatorsV<T>, bool> operator!=(const T& a,
                                                                                                      const T& b) {
	return a == b;
}
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && EnableBitmaskOperatorsV<T>, bool> operator!=(
	const Enumerator<T>& a, const Enumerator<T>& b) {
	return a.Value != b.Value;
}
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && EnableBitmaskOperatorsV<T>, bool> operator!=(
	const Bitmask<T>& a, const Bitmask<T>& b) {
	return a.Value != b.Value;
}
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && EnableBitmaskOperatorsV<T>, bool> operator!=(
	const Enumerator<T>& a, const T& b) {
	return a.Value != b;
}
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && EnableBitmaskOperatorsV<T>, bool> operator!=(
	const T& a, const Enumerator<T>& b) {
	return a != b.Value;
}
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && EnableBitmaskOperatorsV<T>, bool> operator!=(
	const Bitmask<T>& a, const T& b) {
	static_assert(!std::is_same_v<typename Bitmask<T>::UnderlyingT, std::underlying_type_t<T>>,
	              "A bitmask cannot be compared to an enumerator. Use & first.");
	return false;
}
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && EnableBitmaskOperatorsV<T>, bool> operator!=(
	const T& a, const Bitmask<T>& b) {
	static_assert(!std::is_same_v<typename Bitmask<T>::UnderlyingT, std::underlying_type_t<T>>,
	              "A bitmask cannot be compared to an enumerator. Use & first.");
	return false;
}
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && EnableBitmaskOperatorsV<T>, bool> operator!=(
	const Bitmask<T>& a, const Enumerator<T>& b) {
	static_assert(!std::is_same_v<typename Bitmask<T>::UnderlyingT, std::underlying_type_t<T>>,
	              "A bitmask cannot be compared to an enumerator. Use & first.");
	return false;
}
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && EnableBitmaskOperatorsV<T>, bool> operator!=(
	const Enumerator<T>& a, const Bitmask<T>& b) {
	static_assert(!std::is_same_v<typename Bitmask<T>::UnderlyingT, std::underlying_type_t<T>>,
	              "A bitmask cannot be compared to an enumerator. Use & first.");
	return false;
}

#define EnableBitmask(MaskT, BitsT)                         \
	template <>                                               \
	struct EnableBitmaskOperators<BitsT> : std::true_type {}; \
	using MaskT = Bitmask<BitsT>;
}  // namespace Luna
