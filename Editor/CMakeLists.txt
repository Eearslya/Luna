project(Luna-Editor LANGUAGES CXX)

add_executable(Editor)
target_link_libraries(Editor PRIVATE Luna)

target_sources(Editor PRIVATE
	Editor.cpp
	Main.cpp)

add_custom_target(Run
	COMMAND Editor
	DEPENDS Editor
	WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
