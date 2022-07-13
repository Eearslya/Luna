#include "Memory.hpp"

#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#	include <malloc.h>
#endif

namespace Luna {
void* AlignedAlloc(size_t size, size_t align, bool zero) {
	void* ret = nullptr;

#if defined(_WIN32)
	ret = _aligned_malloc(size, align);
#elif defined(_ISOC11_SOURCE)
	ret = aligned_alloc(align, size);
#elif (_POSIX_C_SOURCE >= 200112l) || (_XOPEN_SOURCE >= 600)
	void* ptr = nullptr;
	if (posix_memalign(&ptr, align, size) < 0) { return nullptr; }
	ret = ptr;
#else
	void** place;
	uintptr_t addr = 0;
	void* ptr      = malloc(align + size + sizeof(uintptr_t));

	if (ptr == nullptr) { return nullptr; }

	addr      = (static_cast<uintptr_t>(ptr) + sizeof(uintptr_t) + align) & ~(align - 1);
	place     = reinterpret_cast<void**>(addr);
	place[-1] = ptr;

	ret = reinterpret_cast<void*>(addr);
#endif

	if (ret && zero) { memset(ret, 0, size); }

	return ret;
}

void AlignedFree(void* ptr) {
#if defined(_WIN32)
	_aligned_free(ptr);
#elif !defined(_ISOC11_SOURCE) && !((_POSIX_C_SOURCE >= 200112l) || (_XOPEN_SOURCE >= 600))
	if (ptr != nullptr) {
		void** p = reinterpret_cast<void**>(ptr);
		free(p[-1]);
	}
#else
	free(ptr);
#endif
}
}  // namespace Luna
