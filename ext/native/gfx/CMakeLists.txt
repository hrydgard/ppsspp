set(SRCS
  gl_debug_log.cpp
  gl_lost_manager.cpp
  texture.cpp
  texture_atlas.cpp
  texture_gen.cpp)

set(SRCS ${SRCS})

add_library(gfx STATIC ${SRCS})

if(UNIX)
  add_definitions(-fPIC)
endif(UNIX)
