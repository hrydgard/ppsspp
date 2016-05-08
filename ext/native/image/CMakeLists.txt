set(SRCS
  png_load.cpp
  zim_load.cpp
  zim_save.cpp
)

set(SRCS ${SRCS})

add_library(image STATIC ${SRCS})

if(UNIX)
  add_definitions(-fPIC)
endif(UNIX)

