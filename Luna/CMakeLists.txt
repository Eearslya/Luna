add_library(Luna STATIC)
target_compile_definitions(Luna PRIVATE
	$<$<CONFIG:DEBUG>:LUNA_DEBUG>)
target_include_directories(Luna PUBLIC Include)
target_link_libraries(Luna PUBLIC spdlog)

add_subdirectory(Source)
