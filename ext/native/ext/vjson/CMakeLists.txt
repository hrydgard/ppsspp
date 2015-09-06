cmake_minimum_required(VERSION 2.6)

#if(UNIX)
add_definitions(-fPIC)
add_definitions(-g)
add_definitions(-O2)
add_definitions(-Wall)
#endif(UNIX)

add_library(vjson json.cpp block_allocator.cpp)
