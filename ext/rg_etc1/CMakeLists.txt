cmake_minimum_required(VERSION 2.6)

add_library(etcdec etcdec.cpp)
#if UNIX
add_definitions(-fPIC)

add_library(etcpack etcpack.cpp)

add_definitions(-g)
add_definitions(-O2)
add_definitions(-Wall)
add_definitions(-DSDL)
add_definitions(-Wno-multichar)
# add_definitions(-fno-strict-aliasing)
add_definitions(-fstrict-aliasing)

set(LIBS ${LIBS})

add_executable(etctool etctool.cpp image.cpp)
target_link_libraries(etctool etcdec etcpack)

#add_executable(zimtool zimtool.cpp)
#target_link_libraries(zimtool png freetype z image file zip)

#set(SRCS
#  draw2d.cpp
#  fbo.cpp
#  glsl_program.cpp)
#
#set(SRCS ${SRCS})

#add_library(gfx_es2 STATIC ${SRCS})

#if(UNIX)
#  add_definitions(-fPIC)
#endif(UNIX)
