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
	GIT_TAG 14e5a04e70057972eef8a40df422e30a3b70e4b5)
FetchContent_Declare(json
	GIT_REPOSITORY https://github.com/nlohmann/json.git
	GIT_TAG 6af826d0bdb55e4b69e3ad817576745335f243ca)
FetchContent_Declare(shaderc
	GIT_REPOSITORY https://github.com/google/shaderc.git
	GIT_TAG 4a8f5e537f20bbcfe4b11f1fe45314f1dcbfddf6)
FetchContent_Declare(spdlog
	GIT_REPOSITORY https://github.com/gabime/spdlog.git
	GIT_TAG 834840636cb52242d0fae2d6233c90067a4248dc)
FetchContent_Declare(spirv-cross
	GIT_REPOSITORY https://github.com/KhronosGroup/SPIRV-Cross.git
	GIT_TAG 09e60d74f6a1c8deed2c02c998661b2ec7dd712e)
FetchContent_Declare(spirv-headers
	GIT_REPOSITORY https://github.com/KhronosGroup/SPIRV-Headers
	GIT_TAG 3834d41284f275e8e11f095d5f0fb40ade18abc5)
FetchContent_Declare(spirv-tools
	GIT_REPOSITORY https://github.com/KhronosGroup/SPIRV-Tools
  GIT_TAG 5aab2a8fef46c5e4a2825aedd0715f19ee124a97)
FetchContent_Declare(tracy
	GIT_REPOSITORY https://github.com/wolfpld/tracy.git
	GIT_TAG v0.9.1)
FetchContent_Declare(vma
	GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
	GIT_TAG 73d13a83ede142fa030d84e603a313831fcc424a)

set(ENABLE_GLSLANG_BINARIES Off CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS Off CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES Off CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS Off CACHE BOOL "" FORCE)
set(GLFW_INSTALL Off CACHE BOOL "" FORCE)
set(SHADERC_ENABLE_WERROR_COMPILE Off CACHE BOOL "" FORCE)
set(SHADERC_SKIP_COPYRIGHT_CHECK On CACHE BOOL "" FORCE)
set(SHADERC_SKIP_EXAMPLES On CACHE BOOL "" FORCE)
set(SHADERC_SKIP_INSTALL On CACHE BOOL "" FORCE)
set(SHADERC_SKIP_TESTS On CACHE BOOL "" FORCE)
set(SKIP_SPIRV_TOOLS_INSTALL On CACHE BOOL "" FORCE)
set(SPIRV_CROSS_CLI Off CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_GLSL On CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_HLSL Off CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_MSL Off CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_TESTS Off CACHE BOOL "" FORCE)
set(SPIRV_HEADERS_SKIP_EXAMPLES On CACHE BOOL "" FORCE)
set(SPIRV_HEADERS_SKIP_INSTALL On CACHE BOOL "" FORCE)
set(SPIRV_SKIP_EXECUTABLES On CACHE BOOL "" FORCE)
set(SPIRV_SKIP_TESTS On CACHE BOOL "" FORCE)
set(VMA_STATIC_VULKAN_FUNCTIONS Off CACHE BOOL "" FORCE)
set(VMA_DYNAMIC_VULKAN_FUNCTIONS Off CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(entt glfw glm glslang json spirv-headers spirv-tools shaderc spdlog spirv-cross tracy vma)

add_library(Luna STATIC)
target_compile_definitions(Luna
	PUBLIC GLM_FORCE_DEPTH_ZERO_TO_ONE IMGUI_DEFINE_MATH_OPERATORS VK_ENABLE_BETA_EXTENSIONS VK_NO_PROTOTYPES
	PRIVATE VULKAN_HPP_DISPATCH_LOADER_DYNAMIC)
target_include_directories(Luna PUBLIC "Include")
target_link_libraries(Luna
	PUBLIC EnTT::EnTT glm imgui nlohmann_json spdlog stb TracyClient Vulkan::Headers VulkanMemoryAllocator
	PRIVATE fastgltf glfw glslang shaderc SPIRV SPIRV-Tools spirv-cross-cpp)

add_subdirectory(Source)
