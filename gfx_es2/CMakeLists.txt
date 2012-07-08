set(SRCS
  draw_buffer.cpp
  fbo.cpp
  glsl_program.cpp
  vertex_format.cpp)

set(SRCS ${SRCS})

add_library(gfx_es2 STATIC ${SRCS})

if(UNIX)
  add_definitions(-fPIC)
endif(UNIX)
