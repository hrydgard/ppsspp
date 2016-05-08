set(SRCS
  http_client.cpp
  resolve.cpp)

set(SRCS ${SRCS})

add_library(net STATIC ${SRCS})

if(UNIX)
  add_definitions(-fPIC)
endif(UNIX)
