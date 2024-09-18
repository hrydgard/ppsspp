cmake_minimum_required (VERSION 3.2.0)
project (naett)

set(SRC_DIR ../naett)

set(ALL_SOURCE_FILES
	${SRC_DIR}/naett.c
	)

add_library(naett STATIC ${ALL_SOURCE_FILES})
