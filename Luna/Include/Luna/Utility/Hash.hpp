#pragma once

#include <Luna/Common.hpp>

namespace vk {
template <typename F>
class Flags;
}

namespace Luna {
using Hash = std::uint64_t;

/**
 * Helper class to generate a hash using any hashable data type.
 */
class Hasher {
 public:
	Hasher() = default;
	explicit Hasher(Hash h) : _hash(h) {}
	template <typename T>
	Hasher(const T& data) {
		operator()(data);
	}

	/** Get the computed hash. */
	Hash Get() const {
		return _hash;
	}

	/**
	 * Hash the given block of data.
	 *
	 * Hash is computed by splitting the data into 64-bit chunks and hashing each chunk as if it were a 64-bit integer,
	 * followed by hashing the remaining bytes individually.
	 */
	void Data(size_t size, const void* bytes) {
		const size_t chunks = size / sizeof(uint64_t);

		const uint64_t* p64 = reinterpret_cast<const uint64_t*>(bytes);
		for (size_t i = 0; i < chunks; ++i) { operator()(p64[i]); }

		const uint8_t* p8 = reinterpret_cast<const uint8_t*>(bytes);
		for (size_t i = (chunks * sizeof(uint64_t)); i < size; ++i) { operator()(p8[i]); }
	}

	/** Hash the given object. Must have an std::hash specialization. */
	template <typename T>
	void operator()(const T& data) {
		const std::size_t hash = std::hash<T>{}(data);
		_hash                  = (_hash * 0x100000001b3ull) ^ (hash & 0xffffffffu);
		_hash                  = (_hash * 0x100000001b3ull) ^ (hash >> 32);
	}

	/** Hash the given Vulkan flags. */
	template <typename F>
	void operator()(const vk::Flags<F>& flags) {
		operator()(static_cast<typename F::MaskType>(flags));
	}

 private:
	Hash _hash = 0xcbf29ce484222325ull;
};
}  // namespace Luna
