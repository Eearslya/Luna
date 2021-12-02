add_subdirectory(glfw)
add_subdirectory(physfs)
add_subdirectory(spdlog)
add_subdirectory(vma)

target_include_directories(physfs-static PUBLIC physfs/src)
