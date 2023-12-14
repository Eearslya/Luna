cmake_minimum_required(VERSION 3.21)
project(Luna-Launcher LANGUAGES CXX)

add_executable(Luna-Launcher CrashHandler.cpp Launcher.cpp)
target_link_libraries(Luna-Launcher PRIVATE Luna)
set_target_properties(Luna-Launcher PROPERTIES
  OUTPUT_NAME "Luna"
  VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

add_custom_target(Run
  COMMAND Luna-Launcher
  DEPENDS Luna-Launcher
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
