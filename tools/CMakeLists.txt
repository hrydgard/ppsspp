cmake_minimum_required(VERSION 2.6)

project (Tools)
find_package(PNG REQUIRED)

add_definitions(-O2)
add_definitions(-Wall)
add_definitions(-DSDL)
add_definitions(-Wno-multichar)
add_definitions(-fno-strict-aliasing)
add_definitions(-fopenmp)
# add_definitions(-fstrict-aliasing)

include_directories(..)
include_directories(../math/lin)
include_directories(../image)
include_directories(../ext/libzip)
include_directories(../ext/etcpack)
include_directories(/usr/local/include)
include_directories(${PNG_INCLUDE_DIR})

link_directories(/usr/X11R6/lib)
link_directories(/opt/local/lib)

# Horrible horrible hack
include_directories(/usr/include/freetype2)
include_directories(/usr/local/include/freetype2)
include_directories(/opt/local/include/freetype2)

add_subdirectory(../base base)
add_subdirectory(../gfx gfx)
add_subdirectory(../file file)
add_subdirectory(../image image)
add_subdirectory(../math math)
add_subdirectory(../ext/libzip libzip)
add_subdirectory(../ext/etcpack etcpack)
add_subdirectory(../ext/stb_image stb_image)


add_executable(atlastool atlastool.cpp)
target_link_libraries(atlastool ${PNG_LIBRARY} freetype z image stb_image etcpack etcdec file zip gomp)

add_executable(zimtool zimtool.cpp)
target_link_libraries(zimtool ${PNG_LIBRARY} freetype z image stb_image etcpack etcdec file zip gomp)
