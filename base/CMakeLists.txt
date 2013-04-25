set(SRCS
  colorutil.cpp
  timeutil.cpp
  ../thread/threadutil.cpp
  error_context.cpp
  display.cpp
  buffer.cpp)

add_library(base STATIC ${SRCS})

if(UNIX)
  add_definitions(-fPIC)
endif(UNIX)

add_library(timeutil STATIC timeutil.cpp)

if(UNIX)
  add_definitions(-fPIC)
endif(UNIX)
