cmake_minimum_required(VERSION 3.21)
project(Luna LANGUAGES CXX)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=native")
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")

option(LUNA_BUILD_VIEWER "Build Luna glTF Viewer application" ON)
option(LUNA_DEBUG_VULKAN "Enable Vulkan debugging and validation layers" OFF)

add_subdirectory(External)
add_subdirectory(Luna)

if (LUNA_BUILD_VIEWER)
	add_subdirectory(Viewer)
endif(LUNA_BUILD_VIEWER)
