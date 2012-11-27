set(SRCS
  draw_buffer.cpp
  fbo.cpp
  glsl_program.cpp
  gl_state.cpp
  vertex_format.cpp)

set(SRCS ${SRCS})

add_library(gfx_es2 STATIC ${SRCS})
target_link_libraries(gfx_es2 general gfx)
target_link_libraries(gfx_es2 general file)

if(UNIX)
  add_definitions(-fPIC)
endif(UNIX)
