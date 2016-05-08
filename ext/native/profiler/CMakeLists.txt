set(SRCS
  profiler.cpp
)

# LGUIFileList.cpp

add_library(profiler STATIC ${SRCS})

if(UNIX)
  add_definitions(-fPIC)
endif(UNIX)
