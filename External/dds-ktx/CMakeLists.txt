project(dds-ktx LANGUAGES C)

add_library(dds-ktx STATIC)
target_include_directories(dds-ktx PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_sources(dds-ktx PRIVATE dds-ktx.c)
