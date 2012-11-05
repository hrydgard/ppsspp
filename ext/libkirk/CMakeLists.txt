set(SRCS
    AES.c
    bn.c
    ec.c
    kirk_engine.c
    SHA1.c
)

set(SRCS ${SRCS})

add_library(kirk STATIC ${SRCS})

if(UNIX)
  add_definitions(-fPIC)
endif(UNIX)

