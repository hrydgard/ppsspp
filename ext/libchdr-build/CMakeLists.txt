cmake_minimum_required (VERSION 3.10.0)
project (chdr)

set(LZMA_DIR ../libchdr/deps/lzma-26.02/src)
set(SRC_DIR ../libchdr/src)

include_directories(../libchdr/include)
include_directories(../zlib)
include_directories(../zstd/lib)

# Use our zlib and zstd rather than libchdr's bundled miniz and zstd.
add_definitions(-DCHDR_SYSTEM_ZLIB -DCHDR_SYSTEM_ZSTD)

set(ALL_SOURCE_FILES
	${LZMA_DIR}/LzmaDec.c
	${SRC_DIR}/libchdr_bitstream.c
	${SRC_DIR}/libchdr_cdrom.c
	${SRC_DIR}/libchdr_chd.c
	${SRC_DIR}/libchdr_codec_avhuff.c
	${SRC_DIR}/libchdr_codec_cdfl.c
	${SRC_DIR}/libchdr_codec_cdlz.c
	${SRC_DIR}/libchdr_codec_cdzl.c
	${SRC_DIR}/libchdr_codec_cdzs.c
	${SRC_DIR}/libchdr_codec_flac.c
	${SRC_DIR}/libchdr_codec_huff.c
	${SRC_DIR}/libchdr_codec_lzma.c
	${SRC_DIR}/libchdr_codec_zlib.c
	${SRC_DIR}/libchdr_codec_zstd.c
	${SRC_DIR}/libchdr_flac.c
	${SRC_DIR}/libchdr_huffman.c
	)

add_library(chdr STATIC ${ALL_SOURCE_FILES})
