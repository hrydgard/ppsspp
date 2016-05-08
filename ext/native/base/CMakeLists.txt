set(SRCS
  colorutil.cpp
  timeutil.cpp
  ../thread/threadutil.cpp
  display.cpp
  buffer.cpp
	backtrace.cpp)

add_library(base STATIC ${SRCS})

if(UNIX)
  add_definitions(-fPIC)
endif(UNIX)

add_library(timeutil STATIC timeutil.cpp)

if(UNIX)
  add_definitions(-fPIC)
endif(UNIX)
