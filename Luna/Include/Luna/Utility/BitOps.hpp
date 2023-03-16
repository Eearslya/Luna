#pragma once

#include <cstdint>

#ifdef _MSC_VER
#	include <intrin.h>
#endif

namespace Luna {
#ifdef __GNUC__
#	define LeadingZeroes(x)    ((x) == 0 ? 32 : __builtin_clz(x))
#	define TrailingZeroes(x)   ((x) == 0 ? 32 : __builtin_ctz(x))
#	define TrailingOnes(x)     __builtin_ctz(~uint32_t(x))
#	define LeadingZeroes64(x)  ((x) == 0 ? 64 : __builtin_clzll(x))
#	define TrailingZeroes64(x) ((x) == 0 ? 64 : __builtin_ctzll(x))
#	define TrailingOnes64(x)   __builtin_ctzll(~uint64_t(x))
#elif defined(_MSC_VER)
static inline uint32_t CountLeadingZeroes(uint32_t x) {
	unsigned long result;
	if (_BitScanReverse(&result, x)) { return 31 - result; }
	return 32;
}

static inline uint32_t CountTrailingZeroes(uint32_t x) {
	unsigned long result;
	if (_BitScanForward(&result, x)) { return result; }
	return 32;
}

static inline uint32_t CountLeadingZeroes64(uint64_t x) {
	unsigned long result;
	if (_BitScanReverse64(&result, x)) { return 63 - result; }
	return 64;
}

static inline uint32_t CountTrailingZeroes64(uint64_t x) {
	unsigned long result;
	if (_BitScanForward64(&result, x)) { return result; }
	return 64;
}

#	define LeadingZeroes(x)    ::Luna::CountLeadingZeroes(x)
#	define TrailingZeroes(x)   ::Luna::CountTrailingZeroes(x)
#	define TrailingOnes(x)     ::Luna::CountTrailingZeroes(~uint32_t(x))
#	define LeadingZeroes64(x)  ::Luna::CountLeadingZeroes64(x)
#	define TrailingZeroes64(x) ::Luna::CountTrailingZeroes64(x)
#	define TrailingOnes64(x)   ::Luna::CountTrailingZeroes64(~uint64_t(x))
#endif

template <typename T>
inline void ForEachBit(uint32_t value, const T& func) {
	while (value) {
		const uint32_t bit = TrailingZeroes(value);
		func(bit);
		value &= ~(1u << bit);
	}
}

template <typename T>
inline void ForEachBit64(uint64_t value, const T& func) {
	while (value) {
		const uint32_t bit = TrailingZeroes64(value);
		func(bit);
		value &= ~(1u << bit);
	}
}

template <typename T>
inline void ForEachBitRange(uint32_t value, const T& func) {
	if (value == ~0u) {
		func(0, 32);
		return;
	}

	uint32_t bitOffset = 0;
	while (value) {
		uint32_t bit = TrailingZeroes(value);
		bitOffset += bit;
		value >>= bit;
		uint32_t range = TrailingOnes(value);
		func(bitOffset, range);
		value &= ~((1u << range) - 1);
	}
}
}  // namespace Luna
