set(ARMIPS_REGEXP OFF CACHE BOOL "" FORCE)

add_subdirectory(armips)
if(NOT USING_GLES2)
	add_subdirectory(glew)
endif()

set(ENABLE_GLSLANG_BINARIES OFF CACHE BOOL "let's not build binaries we don't need" FORCE)
set(SPIRV_CROSS_EXCEPTIONS_TO_ASSERTIONS ON CACHE BOOL "let's not use exceptions" FORCE)

# This is really a workaround for an NDK 20 compiler issue (PPSSPP issue #12105), but shouldn't hurt.
if(ANDROID)
set(ENABLE_HLSL OFF CACHE BOOL "let's not build HLSL support we don't need" FORCE)
endif()

add_subdirectory(glslang)
add_subdirectory(snappy)
add_subdirectory(udis86)
add_subdirectory(SPIRV-Cross-build)
if(USE_DISCORD AND NOT IOS)
	add_subdirectory(discord-rpc-build)
endif()
