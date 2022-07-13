project(Luna-Engine LANGUAGES CXX)

include(FetchContent)
include(FindVulkan)

FetchContent_Declare(spdlog
	GIT_REPOSITORY https://github.com/gabime/spdlog.git
	GIT_TAG v1.10.0)

FetchContent_MakeAvailable(spdlog)

add_executable(Luna)
target_compile_definitions(Luna
	PRIVATE VK_ENABLE_BETA_EXTENSIONS VK_NO_PROTOTYPES VULKAN_HPP_DISPATCH_LOADER_DYNAMIC)
target_include_directories(Luna
	PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(Luna
	PRIVATE spdlog vma Vulkan::Headers)

add_subdirectory(Utility)
add_subdirectory(Vulkan)

target_sources(Luna PRIVATE
	Main.cpp)

add_custom_target(Run
	COMMAND Luna
	DEPENDS Luna
	WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
