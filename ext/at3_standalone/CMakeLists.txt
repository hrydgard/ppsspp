cmake_minimum_required (VERSION 3.16)
project (at3_standalone)

set(SRC_DIR .)

# format
set(ALL_SOURCE_FILES
	# rapi
	${SRC_DIR}/atrac.cpp
	${SRC_DIR}/atrac3.cpp
	${SRC_DIR}/atrac3plus.cpp
	${SRC_DIR}/atrac3plusdec.cpp
	${SRC_DIR}/atrac3plusdsp.cpp
	${SRC_DIR}/get_bits.cpp
	${SRC_DIR}/compat.cpp
	${SRC_DIR}/fft.cpp
	${SRC_DIR}/mem.cpp
	)

add_library(at3_standalone STATIC ${ALL_SOURCE_FILES})
