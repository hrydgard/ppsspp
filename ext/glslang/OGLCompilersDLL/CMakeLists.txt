cmake_minimum_required(VERSION 2.8)

set(SOURCES InitializeDll.cpp InitializeDll.h)

add_library(OGLCompiler STATIC ${SOURCES})

if(WIN32)
    source_group("Source" FILES ${SOURCES})
endif(WIN32)

install(TARGETS OGLCompiler 
        ARCHIVE DESTINATION lib)
