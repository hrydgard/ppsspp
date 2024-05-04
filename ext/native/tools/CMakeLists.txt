cmake_minimum_required(VERSION 3.16))
project(Tools)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_definitions(SDL MAX_LOGLEVEL=-1)
add_compile_options(-O2 -Wall -Wno-multichar -fno-strict-aliasing)

# Obsolete code?
if(IOS)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libstdc++")
elseif(APPLE)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++ -mmacosx-version-min=10.7")
endif()

# Find dependencies
find_package(Freetype REQUIRED)
find_package(ZLIB REQUIRED)
find_library(UTIL util REQUIRED)
find_library(ZSTD zstd REQUIRED)

include_directories(${FREETYPE_INCLUDE_DIRS}
	../
	../../
	../../../
)

add_subdirectory(../../libpng17 png17)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/build)

set(COMMON_FILES
	../../../Common/Data/Format/PNGLoad.cpp
	../../../Common/Data/Format/ZIMSave.cpp
)

# Both executables are linked to the same libraries
link_libraries(${FREETYPE_LIBRARIES} ${UTIL} png17 ${ZLIB_LIBRARIES} ${ZSTD})

add_executable(atlastool atlastool.cpp ../../../Common/Data/Encoding/Utf8.cpp ${COMMON_FILES})
add_executable(zimtool zimtool.cpp ${COMMON_FILES})
