include(FindVulkan)

if(NOT Vulkan_FOUND)
	message(FATAL_ERROR "Could not find Vulkan development libraries!")
endif()

add_library(Luna STATIC)
target_compile_definitions(Luna PUBLIC
	$<$<CONFIG:DEBUG>:LUNA_DEBUG> GLFW_INCLUDE_VULKAN TINYGLTF_NO_EXTERNAL_IMAGE TINYGLTF_NO_STB_IMAGE_WRITE TRACY_ENABLE VK_ENABLE_BETA_EXTENSIONS VK_NO_PROTOTYPES VULKAN_HPP_DISPATCH_LOADER_DYNAMIC)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -ftime-trace")
target_include_directories(Luna PUBLIC Include)
target_link_libraries(Luna PUBLIC EnTT::EnTT imgui glfw glm glslang physfs-static spdlog SPIRV spirv-cross-cpp tinygltf tracy Vulkan::Headers vma)
target_precompile_headers(Luna PRIVATE Include/Luna/Core/Core.hpp)

if(MSVC)
    target_compile_options(Luna PUBLIC /arch:SSE4.1 /arch:AVX /arch:AVX2)
else()
    target_compile_options(Luna PUBLIC -msse4.1 -mavx -mavx2)
endif()

add_subdirectory(Source)
