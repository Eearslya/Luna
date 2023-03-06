project(stb LANGUAGES C)

add_library(stb STATIC)
target_include_directories(stb PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_sources(stb PRIVATE stb_image.c)
