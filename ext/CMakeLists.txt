set(ARMIPS_REGEXP OFF CACHE BOOL "" FORCE)
set(ARMIPS_LIBRARY_ONLY ON CACHE BOOL "" FORCE)
if(WIN32)
	set(ARMIPS_USE_STD_FILESYSTEM ON CACHE BOOL "" FORCE)
endif()

if(USE_ARMIPS)
	add_subdirectory(armips)
endif()
if(NOT USING_GLES2)
	add_subdirectory(glew)
endif()

set(ENABLE_GLSLANG_BINARIES OFF CACHE BOOL "let's not build binaries we don't need" FORCE)
set(SPIRV_CROSS_EXCEPTIONS_TO_ASSERTIONS ON CACHE BOOL "let's not use exceptions" FORCE)
set(ENABLE_SPVREMAPPER OFF CACHE BOOL "we don't need spvremapper" FORCE)

# This is really a workaround for an NDK 20 compiler issue (PPSSPP issue #12105), but shouldn't hurt.
if(ANDROID)
set(ENABLE_HLSL OFF CACHE BOOL "let's not build HLSL support we don't need" FORCE)
endif()

add_subdirectory(glslang EXCLUDE_FROM_ALL)
add_subdirectory(snappy)
add_subdirectory(udis86)
add_subdirectory(SPIRV-Cross-build)
if(USE_DISCORD AND NOT IOS AND NOT LIBRETRO)
	add_subdirectory(discord-rpc-build)
endif()
