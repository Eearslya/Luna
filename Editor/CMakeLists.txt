project(Luna-Editor LANGUAGES CXX)

include(FetchContent)

FetchContent_Declare(tinygltf
	GIT_REPOSITORY https://github.com/syoyo/tinygltf.git
	GIT_TAG master)

set(TINYGLTF_BUILD_LOADER_EXAMPLE Off CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(tinygltf)

add_executable(Editor)
target_link_libraries(Editor PRIVATE Luna tinygltf)

target_sources(Editor PRIVATE
	AssetManager.cpp
	ContentBrowserPanel.cpp
	Editor.cpp
	MeshImportPanel.cpp
	SceneHierarchyPanel.cpp
	SceneRenderer.cpp
	UI.cpp)

add_custom_target(Run
	COMMAND Editor
	DEPENDS Editor
	WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
