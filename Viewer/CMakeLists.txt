project(Luna-Viewer LANGUAGES CXX)

add_executable(Viewer)
target_link_libraries(Viewer PRIVATE fastgltf Luna mikktspace)

target_sources(Viewer PRIVATE
	Files.cpp
	ForwardRenderer.cpp
	GBufferRenderer.cpp
	SceneLoader.cpp
	SceneRenderer.cpp
	Viewer.cpp)

add_custom_target(Run
	COMMAND Viewer
	DEPENDS Viewer
	WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
