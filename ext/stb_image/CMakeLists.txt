cmake_minimum_required(VERSION 2.6)

#if(UNIX)
add_definitions(-fPIC)
#endif(UNIX)

add_library(stb_image stb_image.c)
