#pragma once

#include <map>

namespace vk {
template <typename F>
class Flags;
}

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

	void Data(size_t size, const void* bytes) {
		const size_t chunks = size / sizeof(uint64_t);

		// Hash the data in as many 64-bit chunks as we can first.
		const uint64_t* p64 = reinterpret_cast<const uint64_t*>(bytes);
		for (size_t i = 0; i < chunks; ++i) { operator()(p64[i]); }

		// Then hash the remaining bytes one at a time.
		const uint8_t* p8 = reinterpret_cast<const uint8_t*>(bytes);
		for (size_t i = (chunks * sizeof(uint64_t)); i < size; ++i) { operator()(p8[i]); }
	}

	template <typename T>
	void operator()(const T& data) {
		const std::size_t hash = std::hash<T>{}(data);
		_hash                  = (_hash * 0x100000001b3ull) ^ (hash & 0xffffffffu);
		_hash                  = (_hash * 0x100000001b3ull) ^ (hash >> 32);
	}

	template <typename F>
	void operator()(const vk::Flags<F>& flags) {
		operator()(static_cast<uint32_t>(flags));
	}

 private:
	Hash _hash = 0xcbf29ce484222325ull;
};
}  // namespace Luna
