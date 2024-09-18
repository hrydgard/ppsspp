cmake_minimum_required (VERSION 3.16)
project (minimp3)

set(SRC_DIR .)

# format
set(ALL_SOURCE_FILES
	# rapi
	${SRC_DIR}/minimp3.cpp
	)

add_library(minimp3 STATIC ${ALL_SOURCE_FILES})
