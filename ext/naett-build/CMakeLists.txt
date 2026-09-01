cmake_minimum_required (VERSION 3.10.0)
project (naett)

set(SRC_DIR ../naett/src)

set(ALL_SOURCE_FILES
	${SRC_DIR}/naett_core.c
	)

if(WIN32)
	list(APPEND ALL_SOURCE_FILES ${SRC_DIR}/naett_win.c)
elseif(ANDROID)
	list(APPEND ALL_SOURCE_FILES ${SRC_DIR}/naett_android.c)
elseif(APPLE)
	list(APPEND ALL_SOURCE_FILES ${SRC_DIR}/naett_osx.c)
elseif(UNIX)
	list(APPEND ALL_SOURCE_FILES ${SRC_DIR}/naett_linux.c ${SRC_DIR}/naett_curl.c)
endif()

add_library(naett STATIC ${ALL_SOURCE_FILES})
if(WIN32)
	target_link_libraries(naett winhttp)
elseif(UNIX AND NOT APPLE AND NOT ANDROID)
	# Headers only - libcurl itself is dlopen'd, see ../naett/src/naett_curl.h.
	target_include_directories(naett PRIVATE ${CURL_INCLUDE_DIRS})
	target_link_libraries(naett ${CMAKE_DL_LIBS})
endif()
