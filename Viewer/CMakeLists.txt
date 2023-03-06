project(Luna-Viewer LANGUAGES CXX)

add_executable(Viewer)
target_link_libraries(Viewer PRIVATE fastgltf Luna mikktspace stb)

target_sources(Viewer PRIVATE
	Files.cpp
	Model.cpp
	Viewer.cpp)

add_custom_target(Run
	COMMAND Viewer
	DEPENDS Viewer
	WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
