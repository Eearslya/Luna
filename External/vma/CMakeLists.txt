include(FindVulkan)

add_library(vma STATIC vk_mem_alloc.cpp)
target_compile_definitions(vma PUBLIC VMA_STATIC_VULKAN_FUNCTIONS=0 VMA_DYNAMIC_VULKAN_FUNCTIONS=0)
target_include_directories(vma PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(vma PUBLIC Vulkan::Headers)
