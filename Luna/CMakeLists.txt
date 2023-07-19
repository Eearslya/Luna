project(Luna-Engine LANGUAGES CXX)

include(FetchContent)
include(FindVulkan)

add_library(Luna STATIC)
target_include_directories(Luna PUBLIC "Include")
target_precompile_headers(Luna PRIVATE "Include/Luna/Common.hpp")

add_subdirectory(Source)
