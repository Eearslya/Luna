project(Luna-Engine LANGUAGES CXX)

include(FetchContent)
include(FindVulkan)

FetchContent_Declare(spdlog
	GIT_REPOSITORY https://github.com/gabime/spdlog.git
	GIT_TAG 76dfc7e7c0d3c69d3cdaa3399b63545235ccbb02)

FetchContent_MakeAvailable(spdlog)

add_library(Luna STATIC)
target_include_directories(Luna PUBLIC "Include")
target_link_libraries(Luna
  PUBLIC spdlog)
target_precompile_headers(Luna PRIVATE "Include/Luna/Common.hpp")

add_subdirectory(Source)
