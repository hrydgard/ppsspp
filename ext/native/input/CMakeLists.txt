set(SRCS
	input_state.cpp
	gesture_detector.cpp)

set(SRCS ${SRCS})

add_library(input STATIC ${SRCS})
target_link_libraries(input general gfx)
target_link_libraries(input general file)

if(UNIX)
  add_definitions(-fPIC)
endif(UNIX)
