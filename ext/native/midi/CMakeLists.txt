set(SRCS
  midi_input.cpp
)

set(SRCS ${SRCS})

add_library(midi STATIC ${SRCS})

if(UNIX)
  add_definitions(-fPIC)
endif(UNIX)

