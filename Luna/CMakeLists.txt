add_library(Luna STATIC)
target_include_directories(Luna PUBLIC Include)
target_link_libraries(Luna PUBLIC spdlog)

add_subdirectory(Source)