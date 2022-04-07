include(FetchContent)
include(FindVulkan)

FetchContent_Declare(entt
	GIT_REPOSITORY https://github.com/skypjack/entt.git
	GIT_SHALLOW TRUE
	GIT_PROGRESS TRUE
	GIT_TAG master)
FetchContent_Declare(glfw
	GIT_REPOSITORY https://github.com/glfw/glfw.git
	GIT_SHALLOW TRUE
	GIT_PROGRESS TRUE
	GIT_TAG 3.3.6)
FetchContent_Declare(glm
	GIT_REPOSITORY https://github.com/g-truc/glm.git
	GIT_SHALLOW TRUE
	GIT_PROGRESS TRUE
	GIT_TAG master)
FetchContent_Declare(spdlog
	GIT_REPOSITORY https://github.com/gabime/spdlog.git
	GIT_SHALLOW TRUE
	GIT_PROGRESS TRUE
	GIT_TAG v1.x)
FetchContent_Declare(tinygltf
	GIT_REPOSITORY https://github.com/syoyo/tinygltf.git
	GIT_SHALLOW TRUE
	GIT_PROGRESS TRUE
	GIT_TAG master)
FetchContent_Declare(tracy
	GIT_REPOSITORY https://github.com/wolfpld/tracy.git
	GIT_SHALLOW TRUE
	GIT_PROGRESS TRUE
	GIT_TAG v0.8)

set(GLFW_BUILD_DOCS Off CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES Off CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS Off CACHE BOOL "" FORCE)
set(GLFW_INSTALL Off CACHE BOOL "" FORCE)
set(TINYGLTF_BUILD_LOADER_EXAMPLE Off CACHE BOOL "" FORCE)
set(TINYGLTF_BUILD_GL_EXAMPLES Off CACHE BOOL "" FORCE)
set(TINYGLTF_BUILD_VALIDATOR_EXAMPLE Off CACHE BOOL "" FORCE)
set(TINYGLTF_BUILD_BUILDER_EXAMPLE Off CACHE BOOL "" FORCE)
set(TINYGLTF_HEADER_ONLY On CACHE BOOL "" FORCE)
set(TINYGLTF_INSTALL Off CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(entt glfw glm spdlog tracy tinygltf)

add_library(Luna STATIC)

target_compile_definitions(Luna
	PUBLIC $<$<CONFIG:DEBUG>:LUNA_DEBUG> VK_NO_PROTOTYPES VULKAN_HPP_DISPATCH_LOADER_DYNAMIC
	PRIVATE GLFW_INCLUDE_VULKAN TINYGLTF_NO_EXTERNAL_IMAGE TINYGLTF_NO_STB_IMAGE_WRITE TINYGLTF_USE_CPP14 TRACY_ENABLE)
target_include_directories(Luna PUBLIC Include)
target_link_libraries(Luna
	PUBLIC EnTT::EnTT glm imgui spdlog Vulkan::Headers
	PRIVATE glfw TracyClient tinygltf vma)
target_precompile_headers(Luna PRIVATE Include/Luna/Core/Core.hpp)

add_subdirectory(Source)
