#pragma once

#include <Luna/Common.hpp>

namespace Luna {
template <typename Bits>
struct EnableBitmaskOperators : std::false_type {};

template <typename Bits>
concept IsBitmaskType = EnableBitmaskOperators<Bits>::value;

template <typename Bits>
struct Bitflag {
	using UnderlyingT = typename std::underlying_type_t<Bits>;

	Bits Value = {};

	constexpr Bitflag() noexcept = default;
	constexpr Bitflag(Bits value) noexcept : Value(value) {}

	constexpr operator bool() const noexcept {
		return static_cast<UnderlyingT>(Value) != 0;
	}

	constexpr operator Bits() const noexcept {
		return Value;
	}
};

template <typename Bits>
struct Bitmask {
	using UnderlyingT = typename std::underlying_type_t<Bits>;

	UnderlyingT Value;

	constexpr Bitmask() noexcept : Value(static_cast<UnderlyingT>(0)) {}
	constexpr Bitmask(Bits value) noexcept : Value(static_cast<UnderlyingT>(value)) {}
	constexpr Bitmask(UnderlyingT value) noexcept : Value(value) {}
	constexpr Bitmask(Bitflag<Bits> value) noexcept : Value(static_cast<UnderlyingT>(value.Value)) {}
	constexpr Bitmask(const Bitmask<Bits>& other) noexcept = default;
	constexpr Bitmask(Bitmask<Bits>&& other) noexcept      = default;

	constexpr Bitmask<Bits>& operator=(const Bitmask<Bits>& other) noexcept = default;
	constexpr Bitmask<Bits>& operator=(Bitmask<Bits>&& other) noexcept      = default;

	[[nodiscard]] constexpr operator bool() const noexcept {
		return Value != 0;
	}
	[[nodiscard]] constexpr operator UnderlyingT() const noexcept {
		return Value;
	}

	[[nodiscard]] constexpr auto operator<=>(Bits other) const noexcept {
		return *this <=> Bitmask<Bits>(other);
	}
	[[nodiscard]] constexpr auto operator<=>(Bitflag<Bits> other) const noexcept {
		return *this <=> Bitmask<Bits>(other.Value);
	}
	[[nodiscard]] constexpr auto operator<=>(Bitmask<Bits> other) const noexcept {
		return *this <=> other;
	}
	[[nodiscard]] constexpr auto operator<=>(const Bitmask<Bits>& other) const noexcept = default;

	[[nodiscard]] constexpr bool operator!() const noexcept {
		return static_cast<UnderlyingT>(Value) == 0;
	}
	[[nodiscard]] constexpr Bitmask<Bits> operator~() const noexcept {
		return {~static_cast<UnderlyingT>(Value)};
	}

	[[nodiscard]] constexpr Bitmask<Bits> operator&(Bits bits) const noexcept {
		return {static_cast<UnderlyingT>(Value) & static_cast<UnderlyingT>(bits)};
	}
	[[nodiscard]] constexpr Bitmask<Bits> operator&(Bitflag<Bits> bits) const noexcept {
		return {static_cast<UnderlyingT>(Value) & static_cast<UnderlyingT>(bits.Value)};
	}
	[[nodiscard]] constexpr Bitmask<Bits> operator&(Bitmask<Bits> bits) const noexcept {
		return {static_cast<UnderlyingT>(Value) & static_cast<UnderlyingT>(bits.Value)};
	}

	[[nodiscard]] constexpr Bitmask<Bits> operator|(Bits bits) const noexcept {
		return {static_cast<UnderlyingT>(Value) | static_cast<UnderlyingT>(bits)};
	}
	[[nodiscard]] constexpr Bitmask<Bits> operator|(Bitflag<Bits> bits) const noexcept {
		return {static_cast<UnderlyingT>(Value) | static_cast<UnderlyingT>(bits.Value)};
	}
	[[nodiscard]] constexpr Bitmask<Bits> operator|(Bitmask<Bits> bits) const noexcept {
		return {static_cast<UnderlyingT>(Value) | static_cast<UnderlyingT>(bits.Value)};
	}

	[[nodiscard]] constexpr Bitmask<Bits> operator^(Bits bits) const noexcept {
		return {static_cast<UnderlyingT>(Value) ^ static_cast<UnderlyingT>(bits)};
	}
	[[nodiscard]] constexpr Bitmask<Bits> operator^(Bitflag<Bits> bits) const noexcept {
		return {static_cast<UnderlyingT>(Value) ^ static_cast<UnderlyingT>(bits.Value)};
	}
	[[nodiscard]] constexpr Bitmask<Bits> operator^(Bitmask<Bits> bits) const noexcept {
		return {static_cast<UnderlyingT>(Value) ^ static_cast<UnderlyingT>(bits.Value)};
	}

	constexpr Bitmask<Bits>& operator&=(Bits bits) noexcept {
		Value &= static_cast<UnderlyingT>(bits);
		return *this;
	}
	constexpr Bitmask<Bits>& operator&=(Bitflag<Bits> bits) noexcept {
		Value &= static_cast<UnderlyingT>(bits.Value);
		return *this;
	}
	constexpr Bitmask<Bits>& operator&=(Bitmask<Bits> bits) noexcept {
		Value &= static_cast<UnderlyingT>(bits.Value);
		return *this;
	}

	constexpr Bitmask<Bits>& operator|=(Bits bits) noexcept {
		Value |= static_cast<UnderlyingT>(bits);
		return *this;
	}
	constexpr Bitmask<Bits>& operator|=(Bitflag<Bits> bits) noexcept {
		Value |= static_cast<UnderlyingT>(bits.Value);
		return *this;
	}
	constexpr Bitmask<Bits>& operator|=(Bitmask<Bits> bits) noexcept {
		Value |= static_cast<UnderlyingT>(bits.Value);
		return *this;
	}

	constexpr Bitmask<Bits>& operator^=(Bits bits) noexcept {
		Value ^= static_cast<UnderlyingT>(bits);
		return *this;
	}
	constexpr Bitmask<Bits>& operator^=(Bitflag<Bits> bits) noexcept {
		Value ^= static_cast<UnderlyingT>(bits.Value);
		return *this;
	}
	constexpr Bitmask<Bits>& operator^=(Bitmask<Bits> bits) noexcept {
		Value ^= static_cast<UnderlyingT>(bits.Value);
		return *this;
	}
};

[[nodiscard]] constexpr auto operator~(IsBitmaskType auto a) noexcept -> Bitmask<decltype(a)> {
	using UnderlyingT = typename std::underlying_type_t<decltype(a)>;
	return {~static_cast<UnderlyingT>(a)};
}

[[nodiscard]] constexpr auto operator&(IsBitmaskType auto a, IsBitmaskType auto b) noexcept -> Bitmask<decltype(a)> {
	using UnderlyingT = typename std::underlying_type_t<decltype(a)>;
	return {static_cast<UnderlyingT>(a) & static_cast<UnderlyingT>(b)};
}
[[nodiscard]] constexpr auto operator&(IsBitmaskType auto a, Bitmask<decltype(a)> b) noexcept -> Bitmask<decltype(a)> {
	using UnderlyingT = typename std::underlying_type_t<decltype(a)>;
	return {static_cast<UnderlyingT>(a) & static_cast<UnderlyingT>(b.Value)};
}

[[nodiscard]] constexpr auto operator|(IsBitmaskType auto a, IsBitmaskType auto b) noexcept -> Bitmask<decltype(a)> {
	using UnderlyingT = typename std::underlying_type_t<decltype(a)>;
	return {static_cast<UnderlyingT>(a) | static_cast<UnderlyingT>(b)};
}
[[nodiscard]] constexpr auto operator|(IsBitmaskType auto a, Bitmask<decltype(a)> b) noexcept -> Bitmask<decltype(a)> {
	using UnderlyingT = typename std::underlying_type_t<decltype(a)>;
	return {static_cast<UnderlyingT>(a) | static_cast<UnderlyingT>(b.Value)};
}

[[nodiscard]] constexpr auto operator^(IsBitmaskType auto a, IsBitmaskType auto b) noexcept -> Bitmask<decltype(a)> {
	using UnderlyingT = typename std::underlying_type_t<decltype(a)>;
	return {static_cast<UnderlyingT>(a) ^ static_cast<UnderlyingT>(b)};
}
[[nodiscard]] constexpr auto operator^(IsBitmaskType auto a, Bitmask<decltype(a)> b) noexcept -> Bitmask<decltype(a)> {
	using UnderlyingT = typename std::underlying_type_t<decltype(a)>;
	return {static_cast<UnderlyingT>(a) ^ static_cast<UnderlyingT>(b.Value)};
}
}  // namespace Luna
