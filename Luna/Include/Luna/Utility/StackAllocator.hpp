#pragma once

#include <algorithm>

namespace Luna {
template <typename T, size_t N>
class StackAllocator {
 public:
	T* Allocate(size_t count) {
		if (count == 0 || (_index + count) > N) { return nullptr; }

		T* ret = &_buffer[_index];
		_index += count;

		return ret;
	}

	T* AllocateCleared(size_t count) {
		T* ret = Allocate(count);
		if (ret) { std::fill(ret, ret + count, T()); }

		return ret;
	}

	void Reset() {
		_index = 0;
	}

 private:
	T _buffer[N];
	size_t _index = 0;
};
}  // namespace Luna
