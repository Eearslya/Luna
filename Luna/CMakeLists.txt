project(Luna-Engine LANGUAGES CXX)

include(FetchContent)
include(FindVulkan)

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
set(SPIRV_CROSS_CLI Off CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_GLSL On CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_HLSL Off CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_MSL Off CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_TESTS Off CACHE BOOL "" FORCE)
set(VMA_STATIC_VULKAN_FUNCTIONS Off CACHE BOOL "" FORCE)
set(VMA_DYNAMIC_VULKAN_FUNCTIONS Off CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(glfw glm glslang spdlog spirv-cross tracy vma)

add_library(Luna STATIC)
target_compile_definitions(Luna
	PUBLIC GLM_FORCE_DEPTH_ZERO_TO_ONE VK_ENABLE_BETA_EXTENSIONS VK_NO_PROTOTYPES
	PRIVATE VULKAN_HPP_DISPATCH_LOADER_DYNAMIC)
target_include_directories(Luna PUBLIC "Include")
target_link_libraries(Luna
	PUBLIC glm spdlog TracyClient Vulkan::Headers VulkanMemoryAllocator
	PRIVATE glfw glslang SPIRV spirv-cross-cpp)

add_subdirectory(Source)
