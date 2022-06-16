set(ENABLE_GLSLANG_BINARIES Off CACHE BOOL "" FORCE)
set(SPIRV_CROSS_CLI Off CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_TESTS Off CACHE BOOL "" FORCE)
set(TINYGLTF_HEADER_ONLY On CACHE INTERNAL "" FORCE)
set(TINYGLTF_INSTALL Off CACHE INTERNAL "" FORCE)

add_subdirectory(entt)
add_subdirectory(glfw)
add_subdirectory(glm)
add_subdirectory(glslang)
add_subdirectory(spdlog)
add_subdirectory(spirv-cross)
add_subdirectory(tinygltf)
add_subdirectory(vma)

add_library(imgui STATIC)
target_include_directories(imgui PUBLIC imgui imguizmo)
target_sources(imgui PRIVATE
	imgui/imgui.cpp
	imgui/imgui_demo.cpp
	imgui/imgui_draw.cpp
	imgui/imgui_tables.cpp
	imgui/imgui_widgets.cpp
	imguizmo/ImGuizmo.cpp)

add_library(tracy STATIC)
target_compile_definitions(tracy PRIVATE TRACY_ENABLE)
target_include_directories(tracy PUBLIC tracy)
target_sources(tracy PRIVATE tracy/TracyClient.cpp)
