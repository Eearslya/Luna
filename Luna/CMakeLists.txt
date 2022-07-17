project(Luna-Engine LANGUAGES CXX)

include(FetchContent)
include(FindVulkan)

FetchContent_Declare(entt
	GIT_REPOSITORY https://github.com/skypjack/entt.git
	GIT_TAG master)
FetchContent_Declare(glfw
	GIT_REPOSITORY https://github.com/glfw/glfw.git
	GIT_TAG 3.3.6)
FetchContent_Declare(glm
	GIT_REPOSITORY https://github.com/g-truc/glm.git
	GIT_TAG master)
FetchContent_Declare(glslang
	GIT_REPOSITORY https://github.com/KhronosGroup/glslang.git
	GIT_TAG master)
FetchContent_Declare(spdlog
	GIT_REPOSITORY https://github.com/gabime/spdlog.git
	GIT_TAG v1.10.0)
FetchContent_Declare(spirv-cross
	GIT_REPOSITORY https://github.com/KhronosGroup/SPIRV-Cross.git
	GIT_TAG master)
FetchContent_Declare(tinygltf
	GIT_REPOSITORY https://github.com/syoyo/tinygltf.git
	GIT_TAG master)
FetchContent_Declare(yaml
	GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
	GIT_TAG master)

set(ENABLE_GLSLANG_BINARIES Off CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS Off CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES Off CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS Off CACHE BOOL "" FORCE)
set(GLFW_INSTALL Off CACHE BOOL "" FORCE)
set(SPIRV_CROSS_CLI Off CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_GLSL On CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_HLSL Off CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_MSL Off CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_TESTS Off CACHE BOOL "" FORCE)
set(TINYGLTF_BUILD_LOADER_EXAMPLE Off CACHE BOOL "" FORCE)
set(YAML_BUILD_SHARED_LIBS Off CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_TESTS Off CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_TOOLS Off CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(entt glfw glm glslang spdlog spirv-cross tinygltf yaml)

add_executable(Luna)
target_compile_definitions(Luna
	PRIVATE VK_ENABLE_BETA_EXTENSIONS VK_NO_PROTOTYPES VULKAN_HPP_DISPATCH_LOADER_DYNAMIC YAML_CPP_STATIC_DEFINE)
target_include_directories(Luna
	PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(Luna
	PRIVATE EnTT::EnTT glfw glm glslang imgui spdlog SPIRV spirv-cross-cpp tinygltf vma Vulkan::Headers yaml-cpp)

add_subdirectory(Scene)
add_subdirectory(Utility)
add_subdirectory(Vulkan)

target_sources(Luna PRIVATE
	ContentBrowserPanel.cpp
	ImGuiRenderer.cpp
	Main.cpp
	SceneRenderer.cpp)

add_custom_target(Run
	COMMAND Luna
	DEPENDS Luna
	WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
