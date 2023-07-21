target_sources(Luna PRIVATE
  Engine.cpp
  Filesystem.cpp
  Log.cpp
  Threading.cpp
  Window.cpp
  WindowManager.cpp)

if(WIN32)
  add_subdirectory(Windows)
endif(WIN32)
