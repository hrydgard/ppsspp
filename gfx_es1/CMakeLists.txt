set(SRCS
  draw_buffer.cpp
)
set(SRCS ${SRCS})

add_library(gfx_es1 STATIC ${SRCS})

if(UNIX)
  add_definitions(-fPIC)
endif(UNIX)
