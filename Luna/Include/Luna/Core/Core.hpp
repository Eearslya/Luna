/** @file
 *  @brief Most common include directives, for use with precompiled headers */
#pragma once

// Standard Library
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

// Third-party Libraries
#include <spdlog/fmt/fmt.h>

#include <glm/glm.hpp>

#ifdef TRACY_ENABLE
#	include <Tracy.hpp>
#endif

#undef MemoryBarrier
#include <vulkan/vulkan.hpp>
