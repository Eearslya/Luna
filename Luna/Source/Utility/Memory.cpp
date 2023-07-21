#include <Luna/Utility/Memory.hpp>
#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#	include <malloc.h>
#endif

namespace Luna {
void* AllocateAligned(std::size_t size, std::size_t alignment) {
#if defined(_WIN32)
	return _aligned_malloc(size, alignment);
#elif defined(_ISOC11_SOURCE)
	return aligned_alloc(alignment, (size + alignment - 1) & ~(alignment - 1));
#elif (_POSIX_C_SOURCE >= 200112L) || (_XOPEN_SOURCE >= 600)
	void* ptr = nullptr;
	if (posix_memalign(&ptr, alignment, size) < 0) { return nullptr; }
	return ptr;
#else
	void** place;
	uintptr_t addr = 0;
	void* ptr      = malloc(alignment + size + sizeof(uintptr_t));

	if (ptr == nullptr) { return nullptr; }

	addr      = (reinterpret_cast<uintptr_t>(ptr) + sizeof(uintptr_t) + alignment) & ~(alignment - 1);
	place     = reinterpret_cast<void**>(addr);
	place[-1] = ptr;

	return reinterpret_cast<void*>(addr);
#endif
}

void FreeAligned(void* ptr) {
#if defined(_WIN32)
	_aligned_free(ptr);
#elif !defined(_ISOC11_SOURCE) && !((_POSIX_C_SOURCE >= 200112L) || (_XOPEN_SOURCE >= 600))
	if (ptr != nullptr) {
		void** p = reinterpret_cast<void**>(ptr);
		free(p[-1]);
	}
#else
	free(ptr);
#endif
}

void AlignedDeleter::operator()(void* ptr) {
	FreeAligned(ptr);
}
}  // namespace Luna
