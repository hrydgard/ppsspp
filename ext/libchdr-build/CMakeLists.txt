cmake_minimum_required (VERSION 3.2.0)
project (chdr)

set(LZMA_DIR ../libchdr/deps/lzma-22.01/src)
set(SRC_DIR ../libchdr/src)

include_directories(../libchdr/deps/lzma-22.01/include)
include_directories(../libchdr/include)
include_directories(../zlib)
include_directories(../zstd/lib)

add_definitions(-D_7ZIP_ST)

# Hack - CpuArch.c has a workaround that we reuse.
add_definitions(-D__SWITCH__)

set(ALL_SOURCE_FILES
	${LZMA_DIR}/Alloc.c
	${LZMA_DIR}/Bcj2.c
	${LZMA_DIR}/Bcj2Enc.c
	${LZMA_DIR}/Bra.c
	${LZMA_DIR}/Bra86.c
	${LZMA_DIR}/CpuArch.c
	${LZMA_DIR}/Delta.c
	${LZMA_DIR}/LzFind.c
	${LZMA_DIR}/LzFindOpt.c
	${LZMA_DIR}/LzmaDec.c
	${LZMA_DIR}/LzmaEnc.c
	${LZMA_DIR}/Lzma86Dec.c
	${LZMA_DIR}/Lzma86Enc.c
	${LZMA_DIR}/LzmaLib.c
	${LZMA_DIR}/Sort.c
	${SRC_DIR}/libchdr_bitstream.c
	${SRC_DIR}/libchdr_cdrom.c
	${SRC_DIR}/libchdr_chd.c
	${SRC_DIR}/libchdr_flac.c
	${SRC_DIR}/libchdr_huffman.c
	)

add_library(chdr STATIC ${ALL_SOURCE_FILES})
