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

add_subdirectory(glslang EXCLUDE_FROM_ALL)
add_subdirectory(snappy)
add_subdirectory(udis86)
add_subdirectory(SPIRV-Cross-build)
if(USE_DISCORD AND NOT IOS AND NOT LIBRETRO)
	add_subdirectory(discord-rpc-build)
endif()

##
## libchdr dependencies
##
add_library(crypto STATIC
  crypto/md5.c
  crypto/md5.h
  crypto/sha1.c
  crypto/sha1.h
)
include_directories(crypto)

add_library(flac STATIC
  flac-1.3.3/src/libFLAC/bitmath.c
  flac-1.3.3/src/libFLAC/bitreader.c
  flac-1.3.3/src/libFLAC/cpu.c
  flac-1.3.3/src/libFLAC/crc.c
  flac-1.3.3/src/libFLAC/fixed.c
  flac-1.3.3/src/libFLAC/fixed_intrin_sse2.c
  flac-1.3.3/src/libFLAC/fixed_intrin_ssse3.c
  flac-1.3.3/src/libFLAC/float.c
  flac-1.3.3/src/libFLAC/format.c
  flac-1.3.3/src/libFLAC/lpc.c
  flac-1.3.3/src/libFLAC/lpc_intrin_avx2.c
  flac-1.3.3/src/libFLAC/lpc_intrin_sse2.c
  flac-1.3.3/src/libFLAC/lpc_intrin_sse41.c
  flac-1.3.3/src/libFLAC/lpc_intrin_sse.c
  flac-1.3.3/src/libFLAC/lpc_intrin_vsx.c
  flac-1.3.3/src/libFLAC/md5.c
  flac-1.3.3/src/libFLAC/memory.c
  flac-1.3.3/src/libFLAC/metadata_iterators.c
  flac-1.3.3/src/libFLAC/metadata_object.c
  flac-1.3.3/src/libFLAC/stream_decoder.c
  flac-1.3.3/src/libFLAC/window.c
)
include_directories(flac-1.3.3/src/libFLAC/include)
include_directories(flac-1.3.3/include)
set_target_properties(flac PROPERTIES COMPILE_FLAGS "-D'PACKAGE_VERSION=\"1.3.3\"' -DFLAC__HAS_OGG=0 -DFLAC__NO_DLL -DHAVE_LROUND -DHAVE_STDINT_H -DHAVE_STDLIB_H")

add_library(lzma STATIC
  lzma-16.04/C/Alloc.c
  lzma-16.04/C/Alloc.h
  lzma-16.04/C/Bra86.c
  lzma-16.04/C/Bra.c
  lzma-16.04/C/Bra.h
  lzma-16.04/C/BraIA64.c
  lzma-16.04/C/CpuArch.c
  lzma-16.04/C/CpuArch.h
  lzma-16.04/C/Delta.c
  lzma-16.04/C/Delta.h
  lzma-16.04/C/LzFind.c
  lzma-16.04/C/LzFind.h
  lzma-16.04/C/Lzma86Dec.c
  lzma-16.04/C/Lzma86Enc.c
  lzma-16.04/C/Lzma86.h
  lzma-16.04/C/LzmaDec.c
  lzma-16.04/C/LzmaDec.h
  lzma-16.04/C/LzmaEnc.c
  lzma-16.04/C/LzmaEnc.h
  lzma-16.04/C/LzmaLib.c
  lzma-16.04/C/LzmaLib.h
  lzma-16.04/C/Sort.c
  lzma-16.04/C/Sort.h
)
include_directories(lzma-16.04/C)
set_target_properties(lzma PROPERTIES COMPILE_FLAGS "-D_7ZIP_ST")

add_library(chdr STATIC
  libchdr/bitstream.c
  libchdr/bitstream.h
  libchdr/cdrom.c
  libchdr/cdrom.h
  libchdr/chd.c
  libchdr/chd.h
  libchdr/coretypes.h
  libchdr/flac.c
  libchdr/flac.h
  libchdr/huffman.c
  libchdr/huffman.h
)
target_link_libraries(chdr crypto flac lzma ${ZLIB_LIBRARY})
