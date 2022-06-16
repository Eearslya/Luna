#pragma once

#include <immintrin.h>

#include <array>
#include <iostream>
#include <string>

namespace Luna {
class UUID {
 public:
	UUID();
	UUID(const UUID& other);
	UUID(__m128i value);
	UUID(const std::string& str);

	size_t Hash() const;
	std::string ToString() const;

	friend std::ostream& operator<<(std::ostream& oss, const UUID& uuid) {
		oss << uuid.ToString();
		return oss;
	}
	operator std::string() const {
		return ToString();
	}
	bool operator==(const UUID& other) const;
	bool operator!=(const UUID& other) const {
		return !(*this == other);
	}
	bool operator<(const UUID& other) const;
	bool operator>(const UUID& other) const {
		return *this != other && !(*this < other);
	}
	bool operator<=(const UUID& other) const {
		return !(*this > other);
	}
	bool operator>=(const UUID& other) const {
		return !(*this < other);
	}

	static UUID Generate();

 private:
	__m128i _uuid;
};
}  // namespace Luna

namespace std {
template <>
struct hash<Luna::UUID> {
	size_t operator()(const Luna::UUID& uuid) const {
		return uuid.Hash();
	}
};
}  // namespace std
