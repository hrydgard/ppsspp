set(SRCS
  LAMEString.cpp
  colorutil.cpp
  error_context.cpp
  display.cpp
  buffer.cpp)

set(SRCS ${SRCS})

add_library(base STATIC ${SRCS})

if(UNIX)
  add_definitions(-fPIC)
endif(UNIX)

add_library(timeutil STATIC timeutil.cpp)

if(UNIX)
  add_definitions(-fPIC)
endif(UNIX)
