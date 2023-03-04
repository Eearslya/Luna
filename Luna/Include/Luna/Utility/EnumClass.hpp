#pragma once

#include <type_traits>

namespace Luna {
// Templated type used to determine whether logical operators are allowed to be used. Should be specialized for all
// bitmask enum classes to std::true_type.
template <typename Enum>
struct EnableBitmaskOperators : std::false_type {};

// Convenience template to retrieve the true/false value of EnableBitmaskOperators.
template <typename Enum>
constexpr inline bool EnableBitmaskOperatorsV = EnableBitmaskOperators<Enum>::value;

// Represents a single value of a bitmask.
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

// Represents a full bitmask.
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
}  // namespace Luna

// Convenience functions to instantiate bitmask types from a single flag/enumerator.
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && Luna::EnableBitmaskOperatorsV<T>, Luna::Bitmask<T>>
MakeBitmask(const T& t) {
	return {t};
}
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && Luna::EnableBitmaskOperatorsV<T>, Luna::Bitmask<T>>
MakeBitmask(const std::underlying_type_t<T>& t) {
	return {t};
}

// Logical AND operations.
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && Luna::EnableBitmaskOperatorsV<T>, Luna::Enumerator<T>>
operator&(const T& a, const T& b) {
	using UnderlyingT = typename std::underlying_type_t<T>;
	return {static_cast<T>(static_cast<UnderlyingT>(a) & static_cast<UnderlyingT>(b))};
}
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && Luna::EnableBitmaskOperatorsV<T>, Luna::Enumerator<T>>
operator&(const Luna::Enumerator<T>& a, const Luna::Enumerator<T>& b) {
	using UnderlyingT = typename std::underlying_type_t<T>;
	return {static_cast<T>(static_cast<UnderlyingT>(a.Value) & static_cast<UnderlyingT>(b.Value))};
}
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && Luna::EnableBitmaskOperatorsV<T>, Luna::Bitmask<T>> operator&(
	const Luna::Bitmask<T>& a, const Luna::Bitmask<T>& b) {
	return {static_cast<T>(a.Value & b.Value)};
}
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && Luna::EnableBitmaskOperatorsV<T>, Luna::Enumerator<T>>
operator&(const Luna::Bitmask<T>& a, const T& b) {
	using UnderlyingT = typename std::underlying_type_t<T>;
	return {static_cast<T>(a.Value & static_cast<UnderlyingT>(b))};
}
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && Luna::EnableBitmaskOperatorsV<T>, Luna::Enumerator<T>>
operator&(const T& a, const Luna::Bitmask<T>& b) {
	using UnderlyingT = typename std::underlying_type_t<T>;
	return {static_cast<T>(static_cast<UnderlyingT>(a) & b.Value)};
}
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && Luna::EnableBitmaskOperatorsV<T>, Luna::Enumerator<T>>
operator&(const Luna::Bitmask<T>& a, const Luna::Enumerator<T>& b) {
	using UnderlyingT = typename std::underlying_type_t<T>;
	return {static_cast<T>(a.Value & static_cast<UnderlyingT>(b.Value))};
}
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && Luna::EnableBitmaskOperatorsV<T>, Luna::Enumerator<T>>
operator&(const Luna::Enumerator<T>& a, const Luna::Bitmask<T>& b) {
	using UnderlyingT = typename std::underlying_type_t<T>;
	return {static_cast<T>(static_cast<UnderlyingT>(a.Value) & b.Value)};
}
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && Luna::EnableBitmaskOperatorsV<T>, Luna::Enumerator<T>>
operator&(const Luna::Enumerator<T>& a, const T& b) {
	using UnderlyingT = typename std::underlying_type_t<T>;
	return {static_cast<T>(static_cast<UnderlyingT>(a.Value) & static_cast<UnderlyingT>(b))};
}
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && Luna::EnableBitmaskOperatorsV<T>, Luna::Enumerator<T>>
operator&(const T& a, const Luna::Enumerator<T>& b) {
	using UnderlyingT = typename std::underlying_type_t<T>;
	return {static_cast<T>(static_cast<UnderlyingT>(a) & static_cast<UnderlyingT>(b.Value))};
}
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && Luna::EnableBitmaskOperatorsV<T>, Luna::Bitmask<T>&>
operator&=(Luna::Bitmask<T>& a, const T& b) {
	using UnderlyingT = typename std::underlying_type_t<T>;
	a.Value &= static_cast<UnderlyingT>(b);
	return a;
}
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && Luna::EnableBitmaskOperatorsV<T>, Luna::Bitmask<T>&>
operator&=(Luna::Bitmask<T>& a, const Luna::Enumerator<T>& b) {
	using UnderlyingT = typename std::underlying_type_t<T>;
	a.Value &= static_cast<UnderlyingT>(b.Value);
	return a;
}
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && Luna::EnableBitmaskOperatorsV<T>, Luna::Bitmask<T>&>
operator&=(Luna::Bitmask<T>& a, const Luna::Bitmask<T>& b) {
	a.Value &= b.Value;
	return a;
}

// Logical OR operations.
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && Luna::EnableBitmaskOperatorsV<T>, Luna::Enumerator<T>>
operator|(const T& a, const T& b) {
	using UnderlyingT = typename std::underlying_type_t<T>;
	return {static_cast<T>(static_cast<UnderlyingT>(a) | static_cast<UnderlyingT>(b))};
}
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && Luna::EnableBitmaskOperatorsV<T>, Luna::Enumerator<T>>
operator|(const Luna::Enumerator<T>& a, const Luna::Enumerator<T>& b) {
	using UnderlyingT = typename std::underlying_type_t<T>;
	return {static_cast<T>(static_cast<UnderlyingT>(a.Value) | static_cast<UnderlyingT>(b.Value))};
}
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && Luna::EnableBitmaskOperatorsV<T>, Luna::Bitmask<T>> operator|(
	const Luna::Bitmask<T>& a, const Luna::Bitmask<T>& b) {
	return {static_cast<T>(a.Value | b.Value)};
}
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && Luna::EnableBitmaskOperatorsV<T>, Luna::Enumerator<T>>
operator|(const Luna::Bitmask<T>& a, const T& b) {
	using UnderlyingT = typename std::underlying_type_t<T>;
	return {static_cast<T>(a.Value | static_cast<UnderlyingT>(b))};
}
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && Luna::EnableBitmaskOperatorsV<T>, Luna::Enumerator<T>>
operator|(const T& a, const Luna::Bitmask<T>& b) {
	using UnderlyingT = typename std::underlying_type_t<T>;
	return {static_cast<T>(static_cast<UnderlyingT>(a) | b.Value)};
}
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && Luna::EnableBitmaskOperatorsV<T>, Luna::Enumerator<T>>
operator|(const Luna::Bitmask<T>& a, const Luna::Enumerator<T>& b) {
	using UnderlyingT = typename std::underlying_type_t<T>;
	return {static_cast<T>(a.Value | static_cast<UnderlyingT>(b.Value))};
}
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && Luna::EnableBitmaskOperatorsV<T>, Luna::Enumerator<T>>
operator|(const Luna::Enumerator<T>& a, const Luna::Bitmask<T>& b) {
	using UnderlyingT = typename std::underlying_type_t<T>;
	return {static_cast<T>(static_cast<UnderlyingT>(a.Value) | b.Value)};
}
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && Luna::EnableBitmaskOperatorsV<T>, Luna::Enumerator<T>>
operator|(const Luna::Enumerator<T>& a, const T& b) {
	using UnderlyingT = typename std::underlying_type_t<T>;
	return {static_cast<T>(static_cast<UnderlyingT>(a.Value) | static_cast<UnderlyingT>(b))};
}
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && Luna::EnableBitmaskOperatorsV<T>, Luna::Enumerator<T>>
operator|(const T& a, const Luna::Enumerator<T>& b) {
	using UnderlyingT = typename std::underlying_type_t<T>;
	return {static_cast<T>(static_cast<UnderlyingT>(a) | static_cast<UnderlyingT>(b.Value))};
}
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && Luna::EnableBitmaskOperatorsV<T>, Luna::Bitmask<T>&>
operator|=(Luna::Bitmask<T>& a, const T& b) {
	using UnderlyingT = typename std::underlying_type_t<T>;
	a.Value |= static_cast<UnderlyingT>(b);
	return a;
}
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && Luna::EnableBitmaskOperatorsV<T>, Luna::Bitmask<T>&>
operator|=(Luna::Bitmask<T>& a, const Luna::Enumerator<T>& b) {
	using UnderlyingT = typename std::underlying_type_t<T>;
	a.Value |= static_cast<UnderlyingT>(b.Value);
	return a;
}
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && Luna::EnableBitmaskOperatorsV<T>, Luna::Bitmask<T>&>
operator|=(Luna::Bitmask<T>& a, const Luna::Bitmask<T>& b) {
	a.Value |= b.Value;
	return a;
}

// Logical XOR operations.
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && Luna::EnableBitmaskOperatorsV<T>, Luna::Enumerator<T>>
operator^(const T& a, const T& b) {
	using UnderlyingT = typename std::underlying_type_t<T>;
	return {static_cast<T>(static_cast<UnderlyingT>(a) ^ static_cast<UnderlyingT>(b))};
}
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && Luna::EnableBitmaskOperatorsV<T>, Luna::Enumerator<T>>
operator^(const Luna::Enumerator<T>& a, const Luna::Enumerator<T>& b) {
	using UnderlyingT = typename std::underlying_type_t<T>;
	return {static_cast<T>(static_cast<UnderlyingT>(a.Value) ^ static_cast<UnderlyingT>(b.Value))};
}
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && Luna::EnableBitmaskOperatorsV<T>, Luna::Bitmask<T>> operator^(
	const Luna::Bitmask<T>& a, const Luna::Bitmask<T>& b) {
	return {static_cast<T>(a.Value ^ b.Value)};
}
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && Luna::EnableBitmaskOperatorsV<T>, Luna::Enumerator<T>>
operator^(const Luna::Bitmask<T>& a, const T& b) {
	using UnderlyingT = typename std::underlying_type_t<T>;
	return {static_cast<T>(a.Value ^ static_cast<UnderlyingT>(b))};
}
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && Luna::EnableBitmaskOperatorsV<T>, Luna::Enumerator<T>>
operator^(const T& a, const Luna::Bitmask<T>& b) {
	using UnderlyingT = typename std::underlying_type_t<T>;
	return {static_cast<T>(static_cast<UnderlyingT>(a) ^ b.Value)};
}
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && Luna::EnableBitmaskOperatorsV<T>, Luna::Enumerator<T>>
operator^(const Luna::Bitmask<T>& a, const Luna::Enumerator<T>& b) {
	using UnderlyingT = typename std::underlying_type_t<T>;
	return {static_cast<T>(a.Value ^ static_cast<UnderlyingT>(b.Value))};
}
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && Luna::EnableBitmaskOperatorsV<T>, Luna::Enumerator<T>>
operator^(const Luna::Enumerator<T>& a, const Luna::Bitmask<T>& b) {
	using UnderlyingT = typename std::underlying_type_t<T>;
	return {static_cast<T>(static_cast<UnderlyingT>(a.Value) ^ b.Value)};
}
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && Luna::EnableBitmaskOperatorsV<T>, Luna::Enumerator<T>>
operator^(const Luna::Enumerator<T>& a, const T& b) {
	using UnderlyingT = typename std::underlying_type_t<T>;
	return {static_cast<T>(static_cast<UnderlyingT>(a.Value) ^ static_cast<UnderlyingT>(b))};
}
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && Luna::EnableBitmaskOperatorsV<T>, Luna::Enumerator<T>>
operator^(const T& a, const Luna::Enumerator<T>& b) {
	using UnderlyingT = typename std::underlying_type_t<T>;
	return {static_cast<T>(static_cast<UnderlyingT>(a) ^ static_cast<UnderlyingT>(b.Value))};
}
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && Luna::EnableBitmaskOperatorsV<T>, Luna::Bitmask<T>&>
operator^=(Luna::Bitmask<T>& a, const T& b) {
	using UnderlyingT = typename std::underlying_type_t<T>;
	a.Value ^= static_cast<UnderlyingT>(b);
	return a;
}
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && Luna::EnableBitmaskOperatorsV<T>, Luna::Bitmask<T>&>
operator^=(Luna::Bitmask<T>& a, const Luna::Enumerator<T>& b) {
	using UnderlyingT = typename std::underlying_type_t<T>;
	a.Value ^= static_cast<UnderlyingT>(b.Value);
	return a;
}
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && Luna::EnableBitmaskOperatorsV<T>, Luna::Bitmask<T>&>
operator^=(Luna::Bitmask<T>& a, const Luna::Bitmask<T>& b) {
	a.Value ^= b.Value;
	return a;
}

// Logical NOT operations.
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && Luna::EnableBitmaskOperatorsV<T>, Luna::Bitmask<T>> operator~(
	const T& a) {
	using UnderlyingT = typename std::underlying_type_t<T>;
	return {static_cast<T>(~static_cast<UnderlyingT>(a))};
}
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && Luna::EnableBitmaskOperatorsV<T>, Luna::Bitmask<T>> operator~(
	const Luna::Enumerator<T>& a) {
	using UnderlyingT = typename std::underlying_type_t<T>;
	return {static_cast<T>(~static_cast<UnderlyingT>(a.Value))};
}
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && Luna::EnableBitmaskOperatorsV<T>, Luna::Bitmask<T>> operator~(
	const Luna::Bitmask<T>& a) {
	return {static_cast<T>(~a.Value)};
}

// Equality operations.
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && Luna::EnableBitmaskOperatorsV<T>, bool> operator==(
	const T& a, const T& b) {
	return a == b;
}
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && Luna::EnableBitmaskOperatorsV<T>, bool> operator==(
	const Luna::Enumerator<T>& a, const Luna::Enumerator<T>& b) {
	return a.Value == b.Value;
}
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && Luna::EnableBitmaskOperatorsV<T>, bool> operator==(
	const Luna::Bitmask<T>& a, const Luna::Bitmask<T>& b) {
	return a.Value == b.Value;
}
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && Luna::EnableBitmaskOperatorsV<T>, bool> operator==(
	const Luna::Enumerator<T>& a, const T& b) {
	return a.Value == b;
}
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && Luna::EnableBitmaskOperatorsV<T>, bool> operator==(
	const T& a, const Luna::Enumerator<T>& b) {
	return a == b.Value;
}
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && Luna::EnableBitmaskOperatorsV<T>, bool> operator==(
	const Luna::Bitmask<T>& a, const T& b) {
	static_assert(!std::is_same_v<typename Luna::Bitmask<T>::UnderlyingT, std::underlying_type_t<T>>,
	              "A bitmask cannot be compared to an enumerator. Use logical AND operations.");
	return false;
}
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && Luna::EnableBitmaskOperatorsV<T>, bool> operator==(
	const T& a, const Luna::Bitmask<T>& b) {
	static_assert(!std::is_same_v<typename Luna::Bitmask<T>::UnderlyingT, std::underlying_type_t<T>>,
	              "A bitmask cannot be compared to an enumerator. Use logical AND operations.");
	return false;
}
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && Luna::EnableBitmaskOperatorsV<T>, bool> operator==(
	const Luna::Bitmask<T>& a, const Luna::Enumerator<T>& b) {
	static_assert(!std::is_same_v<typename Luna::Bitmask<T>::UnderlyingT, std::underlying_type_t<T>>,
	              "A bitmask cannot be compared to an enumerator. Use logical AND operations.");
	return false;
}
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && Luna::EnableBitmaskOperatorsV<T>, bool> operator==(
	const Luna::Enumerator<T>& a, const Luna::Bitmask<T>& b) {
	static_assert(!std::is_same_v<typename Luna::Bitmask<T>::UnderlyingT, std::underlying_type_t<T>>,
	              "A bitmask cannot be compared to an enumerator. Use logical AND operations.");
	return false;
}

// Inequality operations.
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && Luna::EnableBitmaskOperatorsV<T>, bool> operator!=(
	const T& a, const T& b) {
	return a != b;
}
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && Luna::EnableBitmaskOperatorsV<T>, bool> operator!=(
	const Luna::Enumerator<T>& a, const Luna::Enumerator<T>& b) {
	return a.Value != b.Value;
}
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && Luna::EnableBitmaskOperatorsV<T>, bool> operator!=(
	const Luna::Bitmask<T>& a, const Luna::Bitmask<T>& b) {
	return a.Value != b.Value;
}
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && Luna::EnableBitmaskOperatorsV<T>, bool> operator!=(
	const Luna::Enumerator<T>& a, const T& b) {
	return a.Value != b;
}
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && Luna::EnableBitmaskOperatorsV<T>, bool> operator!=(
	const T& a, const Luna::Enumerator<T>& b) {
	return a != b.Value;
}
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && Luna::EnableBitmaskOperatorsV<T>, bool> operator!=(
	const Luna::Bitmask<T>& a, const T& b) {
	static_assert(!std::is_same_v<typename Luna::Bitmask<T>::UnderlyingT, std::underlying_type_t<T>>,
	              "A bitmask cannot be compared to an enumerator. Use logical AND operations.");
	return false;
}
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && Luna::EnableBitmaskOperatorsV<T>, bool> operator!=(
	const T& a, const Luna::Bitmask<T>& b) {
	static_assert(!std::is_same_v<typename Luna::Bitmask<T>::UnderlyingT, std::underlying_type_t<T>>,
	              "A bitmask cannot be compared to an enumerator. Use logical AND operations.");
	return false;
}
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && Luna::EnableBitmaskOperatorsV<T>, bool> operator!=(
	const Luna::Bitmask<T>& a, const Luna::Enumerator<T>& b) {
	static_assert(!std::is_same_v<typename Luna::Bitmask<T>::UnderlyingT, std::underlying_type_t<T>>,
	              "A bitmask cannot be compared to an enumerator. Use logical AND operations.");
	return false;
}
template <typename T>
constexpr typename std::enable_if_t<std::is_enum_v<T> && Luna::EnableBitmaskOperatorsV<T>, bool> operator!=(
	const Luna::Enumerator<T>& a, const Luna::Bitmask<T>& b) {
	static_assert(!std::is_same_v<typename Luna::Bitmask<T>::UnderlyingT, std::underlying_type_t<T>>,
	              "A bitmask cannot be compared to an enumerator. Use logical AND operations.");
	return false;
}
