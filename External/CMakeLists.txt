add_subdirectory(glfw)
add_subdirectory(physfs)
add_subdirectory(spdlog)

target_include_directories(physfs-static PUBLIC physfs/src)
