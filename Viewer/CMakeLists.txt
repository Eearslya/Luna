project(Luna-Viewer LANGUAGES C CXX)

include(FetchContent)

FetchContent_Declare(libktx
	GIT_REPOSITORY https://github.com/KhronosGroup/KTX-Software.git
	GIT_TAG a08e86e437ebbb3686a730f2ee76d63fa888ec6c)

set(KTX_FEATURE_TESTS Off CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(libktx)

add_executable(Viewer)
target_link_libraries(Viewer PRIVATE dds-ktx fastgltf ktx Luna mikktspace)

target_sources(Viewer PRIVATE
	Files.cpp
	SceneLoader.cpp
	SceneRenderer.cpp
	Viewer.cpp)

add_custom_target(Run
	COMMAND Viewer
	DEPENDS Viewer
	WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
