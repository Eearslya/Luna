add_executable(Editor)

target_sources(Editor PRIVATE
	Editor.cpp)

add_custom_target(Run
	COMMAND Editor
	DEPENDS Editor
	WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
