#pragma once

#include <Luna/Common.hpp>

namespace Luna {
template <typename Bits>
struct EnableBitmaskOperators : std::false_type {};

template <typename Bits>
concept IsBitmaskType = EnableBitmaskOperators<Bits>::value;

template <typename Bits>
struct Bitmask {
	using UnderlyingT = typename std::underlying_type_t<Bits>;

	UnderlyingT Value;

	constexpr Bitmask() noexcept : Value(static_cast<UnderlyingT>(0)) {}
	constexpr Bitmask(Bits value) noexcept : Value(static_cast<UnderlyingT>(value)) {}
	constexpr explicit Bitmask(UnderlyingT value) noexcept : Value(value) {}
	constexpr Bitmask(const Bitmask<Bits>& other) noexcept = default;

	constexpr Bitmask<Bits>& operator=(const Bitmask<Bits>&) noexcept = default;

	[[nodiscard]] explicit constexpr operator UnderlyingT() const noexcept {
		return Value;
	}
	[[nodiscard]] constexpr operator bool() const noexcept {
		return !!Value;
	}
	[[nodiscard]] bool operator!() const noexcept {
		return !Value;
	}
	[[nodiscard]] Bitmask<Bits> operator~() const noexcept {
		return Bitmask<Bits>(~Value);
	}

	[[nodiscard]] auto operator<=>(const Bitmask<Bits>&) const = default;
	[[nodiscard]] constexpr Bitmask<Bits> operator&(const Bitmask<Bits>& other) const noexcept {
		return Bitmask<Bits>(Value & other.Value);
	}
	[[nodiscard]] constexpr Bitmask<Bits> operator|(const Bitmask<Bits>& other) const noexcept {
		return Bitmask<Bits>(Value | other.Value);
	}
	[[nodiscard]] constexpr Bitmask<Bits> operator^(const Bitmask<Bits>& other) const noexcept {
		return Bitmask<Bits>(Value ^ other.Value);
	}

	constexpr Bitmask<Bits>& operator&=(const Bitmask<Bits>& other) noexcept {
		Value &= other.Value;
		return *this;
	}
	constexpr Bitmask<Bits>& operator|=(const Bitmask<Bits>& other) noexcept {
		Value |= other.Value;
		return *this;
	}
	constexpr Bitmask<Bits>& operator^=(const Bitmask<Bits>& other) noexcept {
		Value ^= other.Value;
		return *this;
	}
};

template <typename Bits>
[[nodiscard]] constexpr Bitmask<Bits> operator&(Bits bit, const Bitmask<Bits>& flags) noexcept {
	return flags.operator&(bit);
}
template <typename Bits>
[[nodiscard]] constexpr Bitmask<Bits> operator|(Bits bit, const Bitmask<Bits>& flags) noexcept {
	return flags.operator|(bit);
}
template <typename Bits>
[[nodiscard]] constexpr Bitmask<Bits> operator^(Bits bit, const Bitmask<Bits>& flags) noexcept {
	return flags.operator^(bit);
}

template <typename Bits>
	requires(IsBitmaskType<Bits>)
[[nodiscard]] inline constexpr Bitmask<Bits> operator&(Bits a, Bits b) noexcept {
	return Bitmask<Bits>(a) & b;
}
template <typename Bits>
	requires(IsBitmaskType<Bits>)
[[nodiscard]] inline constexpr Bitmask<Bits> operator|(Bits a, Bits b) noexcept {
	return Bitmask<Bits>(a) | b;
}
template <typename Bits>
	requires(IsBitmaskType<Bits>)
[[nodiscard]] inline constexpr Bitmask<Bits> operator^(Bits a, Bits b) noexcept {
	return Bitmask<Bits>(a) ^ b;
}

template <typename Bits>
	requires(IsBitmaskType<Bits>)
[[nodiscard]] inline constexpr Bitmask<Bits> operator~(Bits bit) noexcept {
	return ~(Bitmask<Bits>(bit));
}
}  // namespace Luna

namespace std {
template <typename T>
struct hash<Luna::Bitmask<T>> {
	size_t operator()(const Luna::Bitmask<T> bitmask) const {
		return hash<typename Luna::Bitmask<T>::UnderlyingT>{}(bitmask);
	}
};
}  // namespace std
