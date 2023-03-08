project(imgui LANGUAGES CXX)

add_library(imgui STATIC)
target_include_directories(imgui PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_sources(imgui PRIVATE imgui.cpp imgui_demo.cpp imgui_draw.cpp imgui_tables.cpp imgui_widgets.cpp)
