set(SRCS
  hash/hash.cpp
  text/utf8.cpp
  text/wrap_text.cpp
)

set(SRCS ${SRCS})

add_library(util STATIC ${SRCS})

if(UNIX)
  add_definitions(-fPIC)
endif(UNIX)

