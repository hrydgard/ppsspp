set(SRCS
    src/AAFilter.cpp
    src/cpu_detect_x86.cpp
    src/FIFOSampleBuffer.cpp
    src/FIRFilter.cpp
    src/InterpolateLinear.cpp
    src/RateTransposer.cpp
    src/SoundTouch.cpp
    src/TDStretch.cpp
    )

include_directories(src include)

add_library(SoundTouch STATIC ${SRCS})

if(UNIX)
  add_definitions(-fPIC)
endif(UNIX)

