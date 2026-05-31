cmake_minimum_required (VERSION 3.13.0)
project (ffmpeg)

include(FindPkgConfig)

# Threads are required
find_package(Threads REQUIRED)

set(SRC_DIR ../../ffmpeg)
set(LIBAVCODEC_SRC_DIR ${SRC_DIR}/libavcodec)
set(LIBAVFORMAT_SRC_DIR ${SRC_DIR}/libavformat)
set(LIBAVUTIL_SRC_DIR ${SRC_DIR}/libavutil)
set(LIBSWRESAMPLE_SRC_DIR ${SRC_DIR}/libswresample)
set(LIBSWSCALE_SRC_DIR ${SRC_DIR}/libswscale)

# Check for exotic cross-compiling configurations such as MinGW sysroot with clang compiler
if(WIN32 AND NOT MSVC)
	set(MINGW_SYSROOT TRUE)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows" AND CMAKE_C_COMPILER_TARGET MATCHES "mingw")
	set(MINGW_SYSROOT TRUE)
endif()

# Additional lib and include dirs for configure checks
set(ADDL_INCLUDES "")
set(ADDL_LIB_DIRS "")

# For cross-compiling, let CMake handle the sysroot via search paths (although toolchains probably already do this)
if(CMAKE_CROSSCOMPILING)
    set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
    set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
    set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
    set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
else()
    if(NOT CMAKE_OSX_SYSROOT AND UNIX AND NOT APPLE)
        list(APPEND ADDL_INCLUDES "/usr/local/include")
        list(APPEND ADDL_LIB_DIRS "/usr/local/lib")
    endif()
endif()

# Silence warnings due to design philosophy differences between ffmpeg and modern compilers
# Taken primarily from meson-ports/ffmpeg's meson.build script at 
# https://gitlab.freedesktop.org/gstreamer/meson-ports/ffmpeg/-/blob/meson-4.4/meson.build?ref_type=heads
if (MSVC AND NOT (CMAKE_C_COMPILER_ID STREQUAL "Clang"))
	add_compile_options(/wd4005 /wd4018 /wd4146 /wd4244 /wd4305 /wd4554 /wd4114 /wd4028 /wd4090 /wd4133)
else()	# Compiler options for both clang and gcc (includes clang-cl in MSVC mode)
	add_compile_options(-D_LARGEFILE_SOURCE -DPIC -Wno-parentheses -Wno-pointer-sign -Wno-switch -Wno-format-truncation -Wno-deprecated-declarations -Wno-unused-function -Wno-uninitialized -Wno-ignored-qualifiers -Wno-unused-variable -Wno-bool-operation -Wno-incompatible-pointer-types -Wno-address -Wno-empty-body)
endif()
if (CMAKE_C_COMPILER_ID STREQUAL "Clang")	# additional options not recognized by gcc
	add_compile_options(-Wno-string-plus-int -Wno-implicit-const-int-float-conversion)
endif()

# Run an abbreviated version of ffmpeg's configure script's checks required to generate usable config.h, 
# libavutil/avconfig.h, and libavutil/ffversion.h headers with hardcoded minimal features required by PPSSPP
include(configure_checks.cmake)

# libavcodec
include(libs/libavcodec.cmake)
add_library(avcodec STATIC ${LIBAVCODEC_SOURCE_FILES})
set_target_properties(avcodec PROPERTIES POSITION_INDEPENDENT_CODE TRUE) # Enable PIC
target_precompile_headers(avcodec PRIVATE ${LIBAVUTIL_SRC_DIR}/internal.h ${LIBAVUTIL_SRC_DIR}/intmath.h)
target_link_libraries(avcodec)
target_include_directories(avcodec PRIVATE ${SRC_DIR} ${CMAKE_CURRENT_BINARY_DIR})
target_include_directories(avcodec INTERFACE ${LIBAVCODEC_HEADERS})	# expose libavcodec headers to PPSSPP build
set(LIBAVCODEC_LIBRARY avcodec)

# libavformat
include(libs/libavformat.cmake)
add_library(avformat STATIC ${LIBAVFORMAT_SOURCE_FILES})
set_target_properties(avformat PROPERTIES POSITION_INDEPENDENT_CODE TRUE) # Enable PIC
target_precompile_headers(avformat PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/config.h ${LIBAVUTIL_SRC_DIR}/internal.h)
target_link_libraries(avformat PUBLIC avcodec)
target_include_directories(avformat PRIVATE ${SRC_DIR} ${CMAKE_CURRENT_BINARY_DIR})
target_include_directories(avformat INTERFACE ${LIBAVFORMAT_HEADERS})
set(LIBAVFORMAT_LIBRARY avformat)

# libavutil
include(libs/libavutil.cmake)
add_library(avutil STATIC ${LIBAVUTIL_SOURCE_FILES})
target_precompile_headers(avutil PRIVATE 
	${CMAKE_CURRENT_BINARY_DIR}/libavutil/ffversion.h 
	${CMAKE_CURRENT_BINARY_DIR}/libavutil/avconfig.h 
	${LIBAVUTIL_SRC_DIR}/internal.h
)
set_target_properties(avutil PROPERTIES POSITION_INDEPENDENT_CODE TRUE) # Enable PIC
target_link_libraries(avutil)
target_include_directories(avutil PRIVATE ${SRC_DIR} ${CMAKE_CURRENT_BINARY_DIR})
target_include_directories(avutil INTERFACE ${LIBAVUTIL_HEADERS})
set(LIBAVUTIL_LIBRARY avutil)

# libswresample
include(libs/libswresample.cmake)
add_library(swresample STATIC ${LIBSWRESAMPLE_SOURCE_FILES})
set_target_properties(swresample PROPERTIES POSITION_INDEPENDENT_CODE TRUE) # Enable PIC
target_precompile_headers(swresample PRIVATE ${LIBAVUTIL_SRC_DIR}/cpu.h ${LIBAVUTIL_SRC_DIR}/internal.h)
target_link_libraries(swresample)
target_include_directories(swresample PUBLIC ${SRC_DIR} ${CMAKE_CURRENT_BINARY_DIR})
target_include_directories(swresample INTERFACE ${LIBSWRESAMPLE_HEADERS})
set(LIBSWRESAMPLE_LIBRARY swresample)

# libswscale
include(libs/libswscale.cmake)
add_library(swscale STATIC ${LIBSWSCALE_SOURCE_FILES})
set_target_properties(swscale PROPERTIES POSITION_INDEPENDENT_CODE TRUE) # Enable PIC
target_precompile_headers(swscale PRIVATE ${LIBAVUTIL_SRC_DIR}/internal.h)
target_link_libraries(swscale)
target_include_directories(swscale PRIVATE ${SRC_DIR} ${CMAKE_CURRENT_BINARY_DIR})
target_include_directories(swscale INTERFACE ${LIBSWSCALE_HEADERS})
set(LIBSWSCALE_LIBRARY swscale)

