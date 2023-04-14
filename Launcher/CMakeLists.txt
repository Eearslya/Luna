cmake_minimum_required(VERSION 3.21)
project(Luna-Launcher LANGUAGES CXX)

add_executable(Luna-Launcher Launcher.cpp)
target_link_libraries(Luna-Launcher PUBLIC Luna)
set_target_properties(Luna-Launcher PROPERTIES OUTPUT_NAME "Luna")

if(WIN32)
	target_link_libraries(Luna-Launcher PRIVATE Dbghelp)
endif(WIN32)

add_custom_target(Run
	COMMAND Luna-Launcher
	DEPENDS Luna-Launcher
	WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
