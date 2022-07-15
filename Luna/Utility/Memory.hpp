#pragma once

#include <cstdlib>

namespace Luna {
void* AlignedAlloc(size_t size, size_t align, bool zero = false);
void AlignedFree(void* ptr);
}  // namespace Luna
