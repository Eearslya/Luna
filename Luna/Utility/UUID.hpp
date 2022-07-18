#pragma once

#include <random>

namespace Luna {
class UUID {
 public:
	UUID();
	UUID(uint64_t uuid);
	UUID(const UUID& other);

	operator uint64_t() {
		return _uuid;
	}
	operator const uint64_t() const {
		return _uuid;
	}

 private:
	uint64_t _uuid;
};
}  // namespace Luna

namespace std {
template <>
struct hash<Luna::UUID> {
	size_t operator()(const Luna::UUID& uuid) const {
		return uint64_t(uuid);
	}
};
}  // namespace std
