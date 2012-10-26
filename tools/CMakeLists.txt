cmake_minimum_required(VERSION 2.6)

project (Tools)
find_package(PNG REQUIRED)



add_definitions(-g)
add_definitions(-O2)
add_definitions(-Wall)
add_definitions(-DSDL)
add_definitions(-Wno-multichar)
add_definitions(-fno-strict-aliasing)
add_definitions(-fopenmp)
# add_definitions(-fstrict-aliasing)

include_directories(../native)
include_directories(../native/base)
include_directories(../native/math/lin)
include_directories(../native/image)
include_directories(../native/ext/libzip)
include_directories(../native/ext/etcpack)
include_directories(/usr/local/include)
include_directories(${PNG_INCLUDE_DIR})

link_directories(/usr/X11R6/lib)
link_directories(/opt/local/lib)

# Horrible horrible hack
include_directories(/usr/include/freetype2)
include_directories(/usr/local/include/freetype2)
include_directories(/opt/local/include/freetype2)

add_subdirectory(../native/base base)
add_subdirectory(../native/gfx gfx)
add_subdirectory(../native/file file)
add_subdirectory(../native/image image)
add_subdirectory(../native/math math)
add_subdirectory(../native/ext/libzip libzip)
add_subdirectory(../native/ext/etcpack etcpack)


add_executable(atlastool atlastool.cpp)
target_link_libraries(atlastool ${PNG_LIBRARY} freetype z image etcpack etcdec file zip gomp)

add_executable(zimtool zimtool.cpp)
target_link_libraries(zimtool ${PNG_LIBRARY} freetype z image etcpack etcdec file zip gomp)
