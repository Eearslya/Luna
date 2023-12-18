#pragma once

namespace Luna {
inline constexpr uint32_t PreviousPowerOfTwo(uint32_t value) {
	uint32_t v = 1;
	while ((v << 1) < value) { v <<= 1; }

	return v;
}
}  // namespace Luna
