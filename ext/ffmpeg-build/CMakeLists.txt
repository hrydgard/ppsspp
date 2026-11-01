cmake_minimum_required (VERSION 3.13.0)
project (ffmpeg C)

# Builds the parts of FFmpeg PPSSPP uses as one static library, from the sources in ext/ffmpeg.
# There's no configure step: config.h is the one it would write, and sources.mk lists the files.

set(FFMPEG_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../ffmpeg)

# FFmpeg leaves calls to disabled components in if (CONFIG_...) blocks for the compiler to drop,
# which MSVC only does when optimizing, so it's optimized in every configuration. These are
# directory variables, so only the targets here get them.
if(MSVC)
	foreach(config DEBUG RELWITHDEBINFO)
		string(REGEX REPLACE "/RTC[1csu]+" "" CMAKE_C_FLAGS_${config} "${CMAKE_C_FLAGS_${config}}")
		string(REPLACE "/Od" "/O2" CMAKE_C_FLAGS_${config} "${CMAKE_C_FLAGS_${config}}")
		string(REPLACE "/Ob0" "" CMAKE_C_FLAGS_${config} "${CMAKE_C_FLAGS_${config}}")
		string(REPLACE "/Ob1" "" CMAKE_C_FLAGS_${config} "${CMAKE_C_FLAGS_${config}}")
	endforeach()
endif()

file(READ sources.mk FFMPEG_SOURCES_MK)
string(REGEX MATCHALL "lib[a-z]+/[A-Za-z0-9_/]+\\.c" FFMPEG_SOURCES "${FFMPEG_SOURCES_MK}")
list(TRANSFORM FFMPEG_SOURCES PREPEND ${FFMPEG_SRC_DIR}/)
# Reconfigure when the list changes.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS sources.mk)

add_library(ffmpeg STATIC ${FFMPEG_SOURCES})
set_target_properties(ffmpeg PROPERTIES POSITION_INDEPENDENT_CODE TRUE)

# config.h is only for FFmpeg itself.
target_include_directories(ffmpeg PUBLIC ${FFMPEG_SRC_DIR})
target_include_directories(ffmpeg PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(ffmpeg PRIVATE HAVE_AV_CONFIG_H _ISOC99_SOURCE _FILE_OFFSET_BITS=64 _LARGEFILE_SOURCE)

find_package(Threads REQUIRED)
target_link_libraries(ffmpeg PRIVATE Threads::Threads)
if(NOT MSVC)
	target_link_libraries(ffmpeg PRIVATE m)
endif()

if(MSVC)
	target_compile_definitions(ffmpeg PRIVATE _USE_MATH_DEFINES _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_WARNINGS)
endif()

# FFmpeg's code predates a lot of what modern compilers warn about.
if(MSVC AND NOT CMAKE_C_COMPILER_ID STREQUAL "Clang")
	target_compile_options(ffmpeg PRIVATE /W0)
else()
	# Except calls to undeclared functions, which mean a wrong HAVE_ in config.h.
	target_compile_options(ffmpeg PRIVATE -w -Werror=implicit-function-declaration)
endif()
