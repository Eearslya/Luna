project(Luna-Engine LANGUAGES CXX)

include(FetchContent)
include(FindVulkan)

FetchContent_Declare(glfw
	GIT_REPOSITORY https://github.com/glfw/glfw.git
	GIT_TAG 3eaf1255b29fdf5c2895856c7be7d7185ef2b241)
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
set(GLFW_VULKAN_STATIC  OFF CACHE BOOL "" FORCE)

FetchContent_Declare(glm
	GIT_REPOSITORY https://github.com/g-truc/glm.git
	GIT_TAG 5c46b9c07008ae65cb81ab79cd677ecc1934b903)

FetchContent_Declare(spdlog
	GIT_REPOSITORY https://github.com/gabime/spdlog.git
	GIT_TAG 76dfc7e7c0d3c69d3cdaa3399b63545235ccbb02)

FetchContent_Declare(vma
	GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
	GIT_TAG 6eb62e1515072827db992c2befd80b71b2d04329)
set(VMA_DYNAMIC_VULKAN_FUNCTIONS OFF CACHE BOOL "" FORCE)
set(VMA_STATIC_VULKAN_FUNCTIONS  OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(glfw glm spdlog vma)

add_library(Luna STATIC)
target_compile_definitions(Luna
  PUBLIC GLM_FORCE_DEPTH_ZERO_TO_ONE VK_ENABLE_BETA_EXTENSIONS VK_NO_PROTOTYPES
  PRIVATE GLFW_INCLUDE_VULKAN VULKAN_HPP_DISPATCH_LOADER_DYNAMIC)
target_include_directories(Luna PUBLIC "Include")
target_link_libraries(Luna
  PUBLIC glm spdlog Vulkan::Headers
  PRIVATE glfw)
target_precompile_headers(Luna PRIVATE "Include/Luna/Common.hpp")

add_subdirectory(Source)
