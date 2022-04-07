add_executable(Editor)

target_link_libraries(Editor PRIVATE Luna)
target_sources(Editor PRIVATE
	Main.cpp)

add_custom_target(Run
	COMMAND Editor
	DEPENDS Editor
	WORKING_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
