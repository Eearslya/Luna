#pragma once

#include <Luna/Common.hpp>

namespace Luna {
void* AllocateAligned(std::size_t size, std::size_t alignment);
void FreeAligned(void* ptr);

struct AlignedDeleter {
	void operator()(void* ptr);
};
}  // namespace Luna
