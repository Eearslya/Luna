#pragma once

#include <cstdint>
#include <new>

namespace Luna {
void* AlignedAlloc(size_t size, size_t alignment);
void* AlignedCalloc(size_t size, size_t alignment);
void AlignedFree(void* ptr);

struct AlignedDeleter {
	void operator()(void* ptr) {
		AlignedFree(ptr);
	}
};

template <typename T>
struct AlignedAllocation {
	static void* operator new(size_t size) {
		void* ret = AlignedAlloc(size, alignof(T));
		if (!ret) { throw std::bad_alloc(); }

		return ret;
	}

	static void* operator new[](size_t size) {
		void* ret = AlignedAlloc(size, alignof(T));
		if (!ret) { throw std::bad_alloc(); }

		return ret;
	}

	static void operator delete(void* ptr) {
		AlignedFree(ptr);
	}

	static void operator delete[](void* ptr) {
		AlignedFree(ptr);
	}
};
}  // namespace Luna
