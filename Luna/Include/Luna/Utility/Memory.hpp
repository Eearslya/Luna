#pragma once

#include <cstdint>

namespace Luna {
void* AlignedAlloc(size_t size, size_t alignment);
void* AlignedCalloc(size_t size, size_t alignment);
void AlignedFree(void* ptr);
}  // namespace Luna
