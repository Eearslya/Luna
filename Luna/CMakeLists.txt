project(Luna-Engine LANGUAGES CXX)

include(FetchContent)
include(FindVulkan)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(glfw
	GIT_REPOSITORY https://github.com/glfw/glfw.git
	GIT_TAG 3eaf1255b29fdf5c2895856c7be7d7185ef2b241)
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
set(GLFW_VULKAN_STATIC  OFF CACHE BOOL "" FORCE)

FetchContent_Declare(glm
	GIT_REPOSITORY https://github.com/g-truc/glm.git
	GIT_TAG 586a402397dd35d66d7a079049856d1e2cbab300)

FetchContent_Declare(glslang
	GIT_REPOSITORY https://github.com/KhronosGroup/glslang.git
	GIT_TAG a8d39f97cdf08cf7eac4ae40dfe7b41420a3be30)
set(ENABLE_CTEST            OFF CACHE BOOL "" FORCE)
set(ENABLE_GLSLANG_BINARIES OFF CACHE BOOL "" FORCE)
set(ENABLE_HLSL              ON CACHE BOOL "" FORCE)
set(SKIP_GLSLANG_INSTALL     ON CACHE BOOL "" FORCE)

FetchContent_Declare(imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui
  GIT_TAG 96b5b1724b677aa03ca4a3d720367beba9a980a3)

FetchContent_Declare(shaderc
	GIT_REPOSITORY https://github.com/google/shaderc.git
	GIT_TAG 0d6f72f3ec57fe68377363d9e810385e6b6e37e1)
set(SHADERC_ENABLE_WERROR_COMPILE OFF CACHE BOOL "" FORCE)
set(SHADERC_SKIP_COPYRIGHT_CHECK   ON CACHE BOOL "" FORCE)
set(SHADERC_SKIP_EXAMPLES          ON CACHE BOOL "" FORCE)
set(SHADERC_SKIP_INSTALL           ON CACHE BOOL "" FORCE)
set(SHADERC_SKIP_TESTS             ON CACHE BOOL "" FORCE)

FetchContent_Declare(spdlog
	GIT_REPOSITORY https://github.com/gabime/spdlog.git
	GIT_TAG ac55e60488032b9acde8940a5de099541c4515da)
set(SPDLOG_USE_STD_FORMAT ON CACHE BOOL "" FORCE)

FetchContent_Declare(spirv-cross
	GIT_REPOSITORY https://github.com/KhronosGroup/SPIRV-Cross.git
	GIT_TAG 4818f7e7ef7b7078a3a7a5a52c4a338e0dda22f4)
set(SPIRV_CROSS_CLI          OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_GLSL   ON CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_HLSL  OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_MSL   OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_TESTS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(spirv-headers
	GIT_REPOSITORY https://github.com/KhronosGroup/SPIRV-Headers
	GIT_TAG 38f39dae5baaa24431b24ac659054ebe972fa1e6)
set(SPIRV_HEADERS_SKIP_EXAMPLES ON CACHE BOOL "" FORCE)
set(SPIRV_HEADERS_SKIP_INSTALL  ON CACHE BOOL "" FORCE)

FetchContent_Declare(spirv-tools
	GIT_REPOSITORY https://github.com/KhronosGroup/SPIRV-Tools
  GIT_TAG fbf047cc8b51d396d66c747f1eee472feb1b2441)
set(SKIP_SPIRV_TOOLS_INSTALL  ON CACHE BOOL "" FORCE)
set(SPIRV_BUILD_FUZZER       OFF CACHE BOOL "" FORCE)
set(SPIRV_SKIP_EXECUTABLES    ON CACHE BOOL "" FORCE)
set(SPIRV_SKIP_TESTS          ON CACHE BOOL "" FORCE)
set(SPIRV_WARN_EVERYTHING    OFF CACHE BOOL "" FORCE)
set(SPIRV_WERROR             OFF CACHE BOOL "" FORCE)

FetchContent_Declare(vma
	GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
	GIT_TAG b721922bce0e536c84b70a1d9bcd5db2b66e42df)
set(VMA_DYNAMIC_VULKAN_FUNCTIONS OFF CACHE BOOL "" FORCE)
set(VMA_STATIC_VULKAN_FUNCTIONS  OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(glfw glm imgui spdlog spirv-headers spirv-tools spirv-cross glslang shaderc vma)

set_target_properties(glslc_exe      PROPERTIES EXCLUDE_FROM_ALL ON)
set_target_properties(shaderc_shared PROPERTIES EXCLUDE_FROM_ALL ON)

add_library(imgui STATIC)
target_compile_definitions(imgui PUBLIC IMGUI_DISABLE_OBSOLETE_FUNCTIONS)
target_include_directories(imgui PUBLIC "${imgui_SOURCE_DIR}")
target_sources(imgui PRIVATE
  "${imgui_SOURCE_DIR}/imgui.cpp"
  "${imgui_SOURCE_DIR}/imgui_demo.cpp"
  "${imgui_SOURCE_DIR}/imgui_draw.cpp"
  "${imgui_SOURCE_DIR}/imgui_tables.cpp"
  "${imgui_SOURCE_DIR}/imgui_widgets.cpp")

add_library(Luna STATIC)
target_compile_definitions(Luna
  PUBLIC GLM_FORCE_DEPTH_ZERO_TO_ONE VK_ENABLE_BETA_EXTENSIONS VK_NO_PROTOTYPES
  PRIVATE GLFW_INCLUDE_VULKAN VULKAN_HPP_DISPATCH_LOADER_DYNAMIC)
target_include_directories(Luna PUBLIC "Include")
target_link_libraries(Luna
  PUBLIC imgui glm spdlog Vulkan::Headers VulkanMemoryAllocator
  PRIVATE glfw shaderc spirv-cross-cpp)
target_precompile_headers(Luna PRIVATE "Include/Luna/Common.hpp")

add_subdirectory(Source)
