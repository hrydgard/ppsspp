set(SRCS
  collision.cpp
  )

add_library(collision STATIC ${SRCS})

if(UNIX)
  add_definitions(-fPIC)
endif(UNIX)
