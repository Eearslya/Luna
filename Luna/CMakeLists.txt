include(FindVulkan)

if(NOT Vulkan_FOUND)
	message(FATAL_ERROR "Could not find Vulkan development libraries!")
endif()

add_library(Luna STATIC)
if (CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
	target_compile_options(Luna PRIVATE -Wshadow)
endif()
target_compile_definitions(Luna PUBLIC
	$<$<CONFIG:DEBUG>:LUNA_DEBUG> GLFW_INCLUDE_VULKAN VK_ENABLE_BETA_EXTENSIONS VK_NO_PROTOTYPES VULKAN_HPP_DISPATCH_LOADER_DYNAMIC)
target_include_directories(Luna PUBLIC Include)
target_link_libraries(Luna PUBLIC imgui glfw glm glslang physfs-static spdlog SPIRV spirv-cross-cpp tinygltf tracy Vulkan::Headers vma)

add_subdirectory(Source)
