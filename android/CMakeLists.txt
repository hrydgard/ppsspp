cmake_minimum_required(VERSION 2.6)

project (PPSSPP)

find_package(PkgConfig)

find_package(PNG REQUIRED)
pkg_check_modules(SDL sdl)

if (APPLE)
  find_library(OGL_LIBRARY OpenGL)
  include_directories(/usr/X11/include)
endif()


include(FindOpenGL)
include(FindSDL)

add_definitions(-g)
add_definitions(-O0)
add_definitions(-Wall)
add_definitions(-DSDL)
add_definitions(-Wno-multichar)
add_definitions(-fno-strict-aliasing)
add_definitions(-DUSE_PROFILER)

include_directories(jni)
include_directories(../native)
include_directories(../native/base)
include_directories(../native/math/lin)
include_directories(../native/audio)
include_directories(../native/ext/libzip)
include_directories(../native/ext/etcpack)
include_directories(/usr/local/include)
include_directories(${SDL_INCLUDE_DIR})
include_directories(${PNG_INCLUDE_DIR})

link_directories(/usr/local/lib)
link_directories(/usr/X11/lib)

add_subdirectory(../native/base base)
add_subdirectory(../native/gfx gfx)
add_subdirectory(../native/profiler profiler)
add_subdirectory(../native/gfx_es2 gfx_es2)
add_subdirectory(../native/file file)
add_subdirectory(../native/image image)
add_subdirectory(../native/ui ui)
add_subdirectory(../native/net net)
add_subdirectory(../native/json jsonwriter)
add_subdirectory(../native/audio audio)
add_subdirectory(../native/math math)
add_subdirectory(../native/ext/libzip libzip)
add_subdirectory(../native/ext/etcpack etcpack)
add_subdirectory(../native/ext/vjson vjson)
add_subdirectory(../native/ext/stb_vorbis stb_vorbis)
add_subdirectory(../native/ext/sha1 sha1)

set(LIBS ${LIBS} ${SDL_LIBRARY} ${OPENGL_LIBRARIES} GLEW file lin ${PNG_LIBRARY} z gfx gfx_es2 ui etcdec image mixer net profiler timeutil file zip base lin vjson stb_vorbis sha1 jsonwriter)

set(FILES 
  jni/ui_atlas.cpp
  jni/ray_intersections.cpp
  jni/collision.cpp
  jni/AudioManager.cpp
  jni/Campaign.cpp
  jni/CampaignEndScreen.cpp
  jni/CalibrateScreen.cpp
  jni/CreditsScreen.cpp
  jni/Curves.cpp
  jni/CollidableMesh.cpp
  jni/DrawList.cpp
  jni/Globals.cpp
  jni/Level.cpp
  jni/CampaignEndScreen.cpp
  jni/Material.cpp
  jni/MainMenuScreen.cpp
  jni/MessageBus.cpp
  jni/Mesh.cpp
  jni/MeshGenerator.cpp
  jni/NativeApp.cpp
  jni/GfxUtil.cpp
  jni/GfxWrapper.cpp
  jni/BallSimulator.cpp
  jni/Node.cpp
  jni/NormalGame.cpp
  jni/NormalGameScreen.cpp
  jni/Scene.cpp
  jni/Sky.cpp
  jni/ShaderManager.cpp
  jni/StickSim.cpp
  jni/TextureManager.cpp
  jni/Transformation.cpp)

if (APPLE)
  SET(FILES ${FILES} jni/SDLMain.m)
endif (APPLE)

add_executable(rollerball ../native/base/PCMain.cpp ${FILES})

target_link_libraries(rollerball ${LIBS})

#add_executable(meshtool jni/Meshtool.cpp ${FILES})

#target_link_libraries(meshtool ${LIBS})
