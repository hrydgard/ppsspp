cmake_minimum_required(VERSION 2.6)

project (Tools)

add_definitions(-O2)
add_definitions(-Wall)
add_definitions(-DSDL)
add_definitions(-Wno-multichar)
add_definitions(-fno-strict-aliasing)
#add_definitions(-fopenmp)

if(IOS)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libstdc++ -DMAX_LOGLEVEL=-1")
elseif(APPLE)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++ -mmacosx-version-min=10.7 -DMAX_LOGLEVEL=-1")
endif()

if(CMAKE_COMPILER_IS_GNUCXX AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 4.7.0)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++0x -DMAX_LOGLEVEL=-1")
else()
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -DMAX_LOGLEVEL=-1")
endif()

# add_definitions(-fstrict-aliasing)

include_directories(..)
include_directories(../..)
include_directories(../../..)

link_directories(/opt/local/lib)

# Horrible horrible hack
include_directories(/usr/include/freetype2)
include_directories(/usr/local/include/freetype2)
include_directories(/opt/local/include/freetype2)
include_directories(/opt/local/include)

add_subdirectory(../../libpng17 png17)

add_executable(build/atlastool atlastool.cpp ../../../Common/Data/Format/PNGLoad.cpp ../../../Common/Data/Encoding/Utf8.cpp ../../../Common/Data/Format/ZIMSave.cpp)
target_link_libraries(build/atlastool freetype util png17 z zstd)

add_executable(build/zimtool zimtool.cpp ../../../Common/Data/Format/PNGLoad.cpp ../../../Common/Data/Format/ZIMSave.cpp)
target_link_libraries(build/zimtool freetype util png17 z zstd)
