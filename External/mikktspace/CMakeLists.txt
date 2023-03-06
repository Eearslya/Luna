project(mikktspace LANGUAGES CXX)

add_library(mikktspace STATIC)
target_include_directories(mikktspace PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_sources(mikktspace PRIVATE mikktspace.cpp)
