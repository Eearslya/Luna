project(Luna-Engine LANGUAGES CXX)

include(FetchContent)
include(FindVulkan)

FetchContent_Declare(entt
	GIT_REPOSITORY https://github.com/skypjack/entt.git
	GIT_TAG e8f85202513f268a6b350d9f2d1f53248cddd2d5)
FetchContent_Declare(glfw
	GIT_REPOSITORY https://github.com/glfw/glfw.git
	GIT_TAG c50d53160fa9b579dda0d0a4f9a7c2512940df8e)
FetchContent_Declare(glm
	GIT_REPOSITORY https://github.com/g-truc/glm.git
	GIT_TAG cc98465e3508535ba8c7f6208df934c156a018dc)
FetchContent_Declare(glslang
	GIT_REPOSITORY https://github.com/KhronosGroup/glslang.git
	GIT_TAG 7e6b7c26a2a565c677b93f1c1769977cadced306)
FetchContent_Declare(spdlog
	GIT_REPOSITORY https://github.com/gabime/spdlog.git
	GIT_TAG 834840636cb52242d0fae2d6233c90067a4248dc)
FetchContent_Declare(spirv-cross
	GIT_REPOSITORY https://github.com/KhronosGroup/SPIRV-Cross.git
	GIT_TAG d8d051381f65b9606fb8016c79b7c3bab872eec3)
FetchContent_Declare(yaml
	GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
	GIT_TAG c73ee34704c512ebe915b283645aefa9f424a22f)

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
set(YAML_BUILD_SHARED_LIBS Off CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_TESTS Off CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_TOOLS Off CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(entt glfw glm glslang spdlog spirv-cross yaml)

add_library(Luna STATIC)
target_compile_definitions(Luna
	PUBLIC GLM_FORCE_DEPTH_ZERO_TO_ONE IMGUI_DEFINE_MATH_OPERATORS VK_ENABLE_BETA_EXTENSIONS VK_NO_PROTOTYPES
	PRIVATE VULKAN_HPP_DISPATCH_LOADER_DYNAMIC YAML_CPP_STATIC_DEFINE $<$<BOOL:${LUNA_DEBUG_VULKAN}>:LUNA_VULKAN_DEBUG>)
target_include_directories(Luna
	PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(Luna
	PUBLIC EnTT::EnTT glm imgui spdlog vma Vulkan::Headers yaml-cpp
	PRIVATE glfw glslang SPIRV spirv-cross-cpp yaml-cpp)

add_subdirectory(Application)
add_subdirectory(Assets)
add_subdirectory(ImGui)
add_subdirectory(Scene)
add_subdirectory(Utility)
add_subdirectory(Vulkan)
