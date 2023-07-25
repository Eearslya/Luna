#pragma once

#include <Luna/Common.hpp>

namespace Luna {
template <typename Bits>
struct EnableBitmaskOperators : std::false_type {};

template <typename Bits>
concept IsBitmaskType = EnableBitmaskOperators<Bits>::value;

template <typename Bits>
class Bitmask {
 public:
	using MaskT = typename std::underlying_type<Bits>::type;

	constexpr Bitmask() noexcept : _mask(0) {}
	constexpr Bitmask(Bits bit) noexcept : _mask(static_cast<MaskT>(bit)) {}
	constexpr Bitmask(const Bitmask<Bits>& other) noexcept = default;
	constexpr explicit Bitmask(MaskT mask) noexcept : _mask(mask) {}

	constexpr Bitmask<Bits>& operator=(const Bitmask<Bits>& other) noexcept = default;

	[[nodiscard]] constexpr operator bool() const noexcept {
		return _mask != 0;
	}
	[[nodiscard]] constexpr operator MaskT() const noexcept {
		return _mask;
	}
	[[nodiscard]] constexpr bool operator!() const noexcept {
		return !_mask;
	}

	[[nodiscard]] constexpr bool operator<(const Bitmask<Bits>& other) const noexcept {
		return _mask < other._mask;
	}
	[[nodiscard]] constexpr bool operator<=(const Bitmask<Bits>& other) const noexcept {
		return _mask <= other._mask;
	}
	[[nodiscard]] constexpr bool operator>(const Bitmask<Bits>& other) const noexcept {
		return _mask > other._mask;
	}
	[[nodiscard]] constexpr bool operator>=(const Bitmask<Bits>& other) const noexcept {
		return _mask >= other._mask;
	}
	[[nodiscard]] constexpr bool operator==(const Bitmask<Bits>& other) const noexcept {
		return _mask == other._mask;
	}
	[[nodiscard]] constexpr bool operator!=(const Bitmask<Bits>& other) const noexcept {
		return _mask != other._mask;
	}

	[[nodiscard]] constexpr Bitmask<Bits> operator&(const Bitmask<Bits>& other) const noexcept {
		return Bitmask<Bits>(_mask & other._mask);
	}
	[[nodiscard]] constexpr Bitmask<Bits> operator|(const Bitmask<Bits>& other) const noexcept {
		return Bitmask<Bits>(_mask | other._mask);
	}
	[[nodiscard]] constexpr Bitmask<Bits> operator^(const Bitmask<Bits>& other) const noexcept {
		return Bitmask<Bits>(_mask ^ other._mask);
	}
	[[nodiscard]] constexpr Bitmask<Bits> operator~() const noexcept {
		return Bitmask<Bits>(~_mask);
	}

	constexpr Bitmask<Bits>& operator&=(const Bitmask<Bits>& other) noexcept {
		_mask &= other._mask;

		return *this;
	}
	constexpr Bitmask<Bits>& operator|=(const Bitmask<Bits>& other) noexcept {
		_mask |= other._mask;

		return *this;
	}
	constexpr Bitmask<Bits>& operator^=(const Bitmask<Bits>& other) noexcept {
		_mask ^= other._mask;

		return *this;
	}

 private:
	MaskT _mask;
};

template <typename Bits>
[[nodiscard]] constexpr bool operator<(Bits bit, const Bitmask<Bits>& flags) noexcept {
	return flags.operator>(bit);
}
template <typename Bits>
[[nodiscard]] constexpr bool operator>(Bits bit, const Bitmask<Bits>& flags) noexcept {
	return flags.operator<(bit);
}
template <typename Bits>
constexpr Bitmask<Bits>& operator<=(Bits bit, Bitmask<Bits>& flags) noexcept {
	return flags.operator<=(bit);
}
template <typename Bits>
constexpr Bitmask<Bits>& operator>=(Bits bit, Bitmask<Bits>& flags) noexcept {
	return flags.operator>=(bit);
}
template <typename Bits>
[[nodiscard]] constexpr Bitmask<Bits>& operator==(Bits bit, Bitmask<Bits>& flags) noexcept {
	return flags.operator==(bit);
}
template <typename Bits>
[[nodiscard]] constexpr Bitmask<Bits>& operator!=(Bits bit, Bitmask<Bits>& flags) noexcept {
	return flags.operator!=(bit);
}

template <typename Bits>
[[nodiscard]] constexpr Bitmask<Bits>& operator&(Bits bit, Bitmask<Bits>& flags) noexcept {
	return flags.operator&(bit);
}
template <typename Bits>
[[nodiscard]] constexpr Bitmask<Bits>& operator|(Bits bit, Bitmask<Bits>& flags) noexcept {
	return flags.operator|(bit);
}
template <typename Bits>
[[nodiscard]] constexpr Bitmask<Bits>& operator^(Bits bit, Bitmask<Bits>& flags) noexcept {
	return flags.operator^(bit);
}

template <IsBitmaskType Bits>
[[nodiscard]] constexpr Bitmask<Bits>& operator&(Bits a, Bits b) noexcept {
	return Bitmask<Bits>(a) & b;
}
template <IsBitmaskType Bits>
[[nodiscard]] constexpr Bitmask<Bits>& operator|(Bits a, Bits b) noexcept {
	return Bitmask<Bits>(a) | b;
}
template <IsBitmaskType Bits>
[[nodiscard]] constexpr Bitmask<Bits>& operator^(Bits a, Bits b) noexcept {
	return Bitmask<Bits>(a) ^ b;
}
template <IsBitmaskType Bits>
[[nodiscard]] constexpr Bitmask<Bits>& operator~(Bits bits) noexcept {
	return ~Bitmask<Bits>(bits);
}
}  // namespace Luna
