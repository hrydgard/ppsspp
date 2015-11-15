cmake_minimum_required(VERSION 2.8)

if(WIN32)
    add_subdirectory(OSDependent/Windows)
elseif(UNIX)
    add_subdirectory(OSDependent/Unix)
else(WIN32)
    message("unkown platform")
endif(WIN32)

set(SOURCES
    MachineIndependent/glslang.y
    MachineIndependent/Constant.cpp
    MachineIndependent/InfoSink.cpp
    MachineIndependent/Initialize.cpp
    MachineIndependent/IntermTraverse.cpp
    MachineIndependent/Intermediate.cpp
    MachineIndependent/ParseHelper.cpp
    MachineIndependent/PoolAlloc.cpp
    MachineIndependent/RemoveTree.cpp
    MachineIndependent/Scan.cpp
    MachineIndependent/ShaderLang.cpp
    MachineIndependent/SymbolTable.cpp
    MachineIndependent/Versions.cpp
    MachineIndependent/intermOut.cpp
    MachineIndependent/limits.cpp
    MachineIndependent/linkValidate.cpp
    MachineIndependent/parseConst.cpp
    MachineIndependent/reflection.cpp
    MachineIndependent/preprocessor/Pp.cpp
    MachineIndependent/preprocessor/PpAtom.cpp
    MachineIndependent/preprocessor/PpContext.cpp
    MachineIndependent/preprocessor/PpMemory.cpp
    MachineIndependent/preprocessor/PpScanner.cpp
    MachineIndependent/preprocessor/PpSymbols.cpp
    MachineIndependent/preprocessor/PpTokens.cpp
    GenericCodeGen/CodeGen.cpp
    GenericCodeGen/Link.cpp)

set(HEADERS
    Public/ShaderLang.h
    Include/arrays.h
    Include/BaseTypes.h
    Include/Common.h
    Include/ConstantUnion.h
    Include/InfoSink.h
    Include/InitializeGlobals.h
    Include/intermediate.h
    Include/PoolAlloc.h
    Include/ResourceLimits.h
    Include/revision.h
    Include/ShHandle.h
    Include/Types.h
    MachineIndependent/gl_types.h
    MachineIndependent/Initialize.h
    MachineIndependent/localintermediate.h
    MachineIndependent/ParseHelper.h
    MachineIndependent/reflection.h
    MachineIndependent/RemoveTree.h
    MachineIndependent/Scan.h
    MachineIndependent/ScanContext.h
    MachineIndependent/SymbolTable.h
    MachineIndependent/Versions.h
    MachineIndependent/preprocessor/PpContext.h
    MachineIndependent/preprocessor/PpTokens.h)

find_package(BISON)
if(NOT BISON_FOUND)
    set(BISON_EXECUTABLE ../tools/bison.exe)
    message("bison not found. Assuming it is at ${BISON_EXECUTABLE}")
endif()

# Always use a custom command since our use of --defines isn't assumed by CMake's BISON_TARGET,
# which ends up causing the target to always be rebuilt.
add_custom_command(OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/MachineIndependent/glslang_tab.cpp ${CMAKE_CURRENT_SOURCE_DIR}/MachineIndependent/glslang_tab.cpp.h
                   COMMAND ${BISON_EXECUTABLE} --defines=${CMAKE_CURRENT_SOURCE_DIR}/MachineIndependent/glslang_tab.cpp.h -t ${CMAKE_CURRENT_SOURCE_DIR}/MachineIndependent/glslang.y -o ${CMAKE_CURRENT_SOURCE_DIR}/MachineIndependent/glslang_tab.cpp
                   MAIN_DEPENDENCY MachineIndependent/glslang.y
                   WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
set(BISON_GLSLParser_OUTPUT_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/MachineIndependent/glslang_tab.cpp)

add_library(glslang STATIC ${BISON_GLSLParser_OUTPUT_SOURCE} ${SOURCES} ${HEADERS})

if(WIN32)
    source_group("Public" REGULAR_EXPRESSION "Public/*")
    source_group("MachineIndependent" REGULAR_EXPRESSION "MachineIndependent/[^/]*")
    source_group("Generated Files" FILES MachineIndependent/glslang_tab.cpp MachineIndependent/glslang_tab.cpp.h)
    source_group("Include" REGULAR_EXPRESSION "Include/[^/]*")
    source_group("GenericCodeGen" REGULAR_EXPRESSION "GenericCodeGen/*")
    source_group("MachineIndependent\\Preprocessor" REGULAR_EXPRESSION "MachineIndependent/preprocessor/*")
endif(WIN32)

install(TARGETS glslang 
        ARCHIVE DESTINATION lib)
