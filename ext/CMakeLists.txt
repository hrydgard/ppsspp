set(ARMIPS_REGEXP OFF CACHE BOOL "" FORCE)

add_subdirectory(armips)
if(NOT USING_GLES2)
	add_subdirectory(glew)
endif()

set(ENABLE_GLSLANG_BINARIES OFF CACHE BOOL "let's not build binaries we don't need" FORCE)
set(SPIRV_CROSS_EXCEPTIONS_TO_ASSERTIONS ON CACHE BOOL "let's not use exceptions" FORCE)

add_subdirectory(glslang)
add_subdirectory(snappy)
add_subdirectory(udis86)
add_subdirectory(SPIRV-Cross-build)
if(USE_DISCORD AND NOT IOS)
	add_subdirectory(discord-rpc-build)
endif()
