project(Luna-Engine LANGUAGES CXX)

include(FetchContent)

FetchContent_Declare(glfw
	GIT_REPOSITORY https://github.com/glfw/glfw.git
	GIT_TAG c50d53160fa9b579dda0d0a4f9a7c2512940df8e)
FetchContent_Declare(glm
	GIT_REPOSITORY https://github.com/g-truc/glm.git
	GIT_TAG cc98465e3508535ba8c7f6208df934c156a018dc)

set(GLFW_BUILD_DOCS Off CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES Off CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS Off CACHE BOOL "" FORCE)
set(GLFW_INSTALL Off CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(glfw glm)

add_library(Luna STATIC)
target_compile_definitions(Luna
	PUBLIC GLM_FORCE_DEPTH_ZERO_TO_ONE)
target_include_directories(Luna PUBLIC "Include")
target_link_libraries(Luna
	PUBLIC glm
	PRIVATE glfw)

add_subdirectory(Source)
