#pragma once

#include <map>

namespace Luna {
using Hash = std::uint64_t;

class Hasher {
 public:
	Hasher() = default;
	explicit Hasher(Hash h) : _hash(h) {}
	template <typename T>
	Hasher(const T& data) {
		operator()(data);
	}

	Hash Get() const {
		return _hash;
	}

	template <typename T>
	void operator()(const T& data) {
		const std::size_t hash = std::hash<T>{}(data);
		_hash                  = (_hash * 0x100000001b3ull) ^ (hash & 0xffffffffu);
		_hash                  = (_hash * 0x100000001b3ull) ^ (hash >> 32);
	}

 private:
	Hash _hash = 0xcbf29ce484222325ull;
};
}  // namespace Luna
