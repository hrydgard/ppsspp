set(SRCS
  draw_buffer.cpp
	draw_text.cpp
  glsl_program.cpp
	gpu_features.cpp
  gl_state.cpp)

set(SRCS ${SRCS})

add_library(gfx_es2 STATIC ${SRCS})
target_link_libraries(gfx_es2 general gfx)
target_link_libraries(gfx_es2 general file)

if(UNIX)
  add_definitions(-fPIC)
endif(UNIX)
