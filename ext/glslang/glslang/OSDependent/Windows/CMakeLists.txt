cmake_minimum_required(VERSION 2.8)

set(SOURCES ossource.cpp ../osinclude.h)

add_library(OSDependent STATIC ${SOURCES})

if(WIN32)
    source_group("Source" FILES ${SOURCES})
endif(WIN32)

install(TARGETS OSDependent 
        ARCHIVE DESTINATION lib)
