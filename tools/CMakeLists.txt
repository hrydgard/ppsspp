cmake_minimum_required(VERSION 2.6)

project (Tools)
find_package(PNG REQUIRED)

add_definitions(-O2)
add_definitions(-Wall)
add_definitions(-DSDL)
add_definitions(-Wno-multichar)
add_definitions(-fno-strict-aliasing)
add_definitions(-fopenmp)

if(IOS)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libstdc++")
elseif(APPLE)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++ -mmacosx-version-min=10.7")
endif()

if(CMAKE_COMPILER_IS_GNUCXX AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 4.7.0)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++0x")
else()
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
endif()

# add_definitions(-fstrict-aliasing)

include_directories(..)
include_directories(../math/lin)
include_directories(../image)
include_directories(../ext/libzip)
include_directories(../ext/rg_etc1)
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
add_subdirectory(../util util)
add_subdirectory(../ext/libzip libzip)
add_subdirectory(../ext/rg_etc1 rg_etc1)
add_subdirectory(../ext/stb_image stb_image)


add_executable(atlastool atlastool.cpp)
target_link_libraries(atlastool ${PNG_LIBRARY} freetype util z image stb_image rg_etc1 file zip gomp)

add_executable(zimtool zimtool.cpp)
target_link_libraries(zimtool ${PNG_LIBRARY} freetype z image stb_image rg_etc1 file zip gomp)
