set(SRCS
  json_writer.cpp)

set(SRCS ${SRCS})

add_library(jsonwriter STATIC ${SRCS})



add_executable(json_writer_test json_writer_test.cpp)
target_link_libraries(json_writer_test jsonwriter)

if(UNIX)
  add_definitions(-fPIC)
endif(UNIX)
