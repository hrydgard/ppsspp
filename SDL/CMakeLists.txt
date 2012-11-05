cmake_minimum_required(VERSION 2.6)

project (PPSSPP)

find_package(PkgConfig)

find_package(PNG REQUIRED)
pkg_check_modules(SDL sdl)

if (APPLE)
	find_library(OGL_LIBRARY OpenGL)
	include_directories(/usr/X11/include)
endif()

# TODO: Rely on compiler define instead. __BLACKBERRY__ ?
if (BLACKBERRY)
    add_definitions(-DBLACKBERRY)
endif()


include(FindOpenGL)
include(FindSDL)

add_definitions(-g)
# add_definitions(-O2)
add_definitions(-Wall)
add_definitions(-DSDL)
add_definitions(-Wno-multichar)
add_definitions(-fno-strict-aliasing)
add_definitions(-DUSE_PROFILER)
if (NOT BLACKBERRY)
    add_definitions(-D_DEBUG)
endif()

if (UNIX)
	if (NOT APPLE)  # can't build the SDL .m file with -std=gnu++0x
	    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=gnu++0x")
	endif()
endif()

include_directories(..)
include_directories(../Common)
include_directories(../native)
include_directories(../native/ext/libzip)
include_directories(/opt/local/include)
include_directories(/usr/local/include)
include_directories(${SDL_INCLUDE_DIR})
include_directories(${PNG_INCLUDE_DIR})

link_directories(/usr/local/lib)
link_directories(/opt/local/lib)
link_directories(/usr/X11/lib)

add_subdirectory(../native/base base)
add_subdirectory(../native/gfx gfx)
add_subdirectory(../native/profiler profiler)
add_subdirectory(../native/gfx_es2 gfx_es2)
add_subdirectory(../native/file file)
add_subdirectory(../native/image image)
add_subdirectory(../native/json jsonwriter)
add_subdirectory(../native/audio audio)
add_subdirectory(../native/math math)
add_subdirectory(../native/net net)
add_subdirectory(../native/ui ui)
add_subdirectory(../native/ext/libzip libzip)
add_subdirectory(../native/ext/etcpack etcpack)
add_subdirectory(../native/ext/vjson vjson)
add_subdirectory(../native/ext/stb_vorbis stb_vorbis)
add_subdirectory(../native/ext/stb_image stb_image)
add_subdirectory(../native/ext/sha1 sha1)
add_subdirectory(../Common Common)
add_subdirectory(../GPU GPU)
add_subdirectory(../Core Core)

set(LIBS ${LIBS} ${SDL_LIBRARY} ${OPENGL_LIBRARIES} file lin ${PNG_LIBRARY} z gfx gfx_es2 etcdec image stb_image mixer net ui profiler timeutil file zip base lin vjson stb_vorbis sha1 jsonwriter common core gpu)

# TODO: Blackberry specific libs (eg. TCO)
if(BLACKBERRY)
    set(LIBS ${LIBS} socket)
else()
    set(LIBS ${LIBS} GLEW)
endif()

set(FILES
	../android/jni/NativeApp.cpp
	../android/jni/EmuScreen.cpp
	../android/jni/MenuScreens.cpp
	../android/jni/GamepadEmu.cpp
	../android/jni/UIShader.cpp
	../android/jni/ui_atlas.cpp
	../native/base/PCMain.cpp
	)

if(APPLE)
	set(FILES ${FILES} SDLMain.m)
endif(APPLE)

add_executable(ppsspp ${FILES})

target_link_libraries(ppsspp ${LIBS})

set(FILES ../headless/Headless.cpp)

add_executable(ppsspp-headless ${FILES})

target_link_libraries(ppsspp-headless ${LIBS})

