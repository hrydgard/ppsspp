set(SRCS
  colorutil.cpp
  ../thread/threadutil.cpp
  display.cpp
  buffer.cpp
)

add_library(base STATIC ${SRCS})

if(UNIX)
  add_definitions(-fPIC)
endif(UNIX)
