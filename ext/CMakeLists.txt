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

# This works around a bug in cpu_features when ${CMAKE_BINARY_DIR} != ${CMAKE_CURRENT_BINRARY_DIR}.
# We also disable the list_cpu_features binary.
add_subdirectory(cmake/cpu_features EXCLUDE_FROM_ALL)

set(SKIP_GLSLANG_INSTALL ON CACHE BOOL "" FORCE)
set(ENABLE_GLSLANG_INSTALL OFF)
add_subdirectory(glslang EXCLUDE_FROM_ALL)
add_subdirectory(snappy)
add_subdirectory(minimp3)
add_subdirectory(at3_standalone)
add_subdirectory(udis86)
add_subdirectory(SPIRV-Cross-build)
add_subdirectory(rcheevos-build)
if(NOT HTTPS_NOT_AVAILABLE)
	add_subdirectory(naett-build)
endif()
if(USE_DISCORD AND NOT IOS AND NOT LIBRETRO)
	add_subdirectory(discord-rpc-build)
endif()

add_subdirectory(libchdr-build)

if(ANDROID)
    if (ARM64)
        add_subdirectory(libadrenotools)
    endif()
endif()
