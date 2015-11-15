cmake_minimum_required(VERSION 2.8)

set(CMAKE_INSTALL_PREFIX "install" CACHE STRING "prefix")

project(glslang)

if(WIN32)
    include(ChooseMSVCCRT.cmake)
    add_definitions(-DGLSLANG_OSINCLUDE_WIN32)
elseif(UNIX)
    add_definitions(-fPIC)
    add_definitions(-DGLSLANG_OSINCLUDE_UNIX)
else(WIN32)
    message("unkown platform")
endif(WIN32)

if(CMAKE_COMPILER_IS_GNUCXX)
    add_definitions(-std=c++11)
elseif(${CMAKE_CXX_COMPILER_ID} MATCHES "Clang")
    add_definitions(-std=c++11)
endif()

add_subdirectory(glslang)
add_subdirectory(OGLCompilersDLL)
add_subdirectory(StandAlone)
add_subdirectory(SPIRV)
