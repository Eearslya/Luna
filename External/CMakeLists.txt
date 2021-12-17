set(SPIRV_CROSS_CLI Off CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_TESTS Off CACHE BOOL "" FORCE)

add_subdirectory(glfw)
add_subdirectory(physfs)
add_subdirectory(spdlog)
add_subdirectory(spirv-cross)
add_subdirectory(tinygltf)
add_subdirectory(vma)

target_include_directories(physfs-static PUBLIC physfs/src)
