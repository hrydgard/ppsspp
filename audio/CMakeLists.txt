set(SRCS
  mixer.cpp
  wav_read.cpp)

set(SRCS ${SRCS})

add_library(mixer STATIC ${SRCS})

if(UNIX)
  add_definitions(-fPIC)
endif(UNIX)
