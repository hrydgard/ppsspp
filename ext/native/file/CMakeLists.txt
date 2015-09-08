set(SRCS
  easy_file.cpp
  chunk_file.cpp
  zip_read.cpp
  file_util.cpp
  dialog.cpp)

set(SRCS ${SRCS})

add_library(file STATIC ${SRCS})
target_link_libraries(file general zip)

if(UNIX)
  add_definitions(-fPIC)
endif(UNIX)
