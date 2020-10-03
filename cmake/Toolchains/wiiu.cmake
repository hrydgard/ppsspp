set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR powerpc)
set(WIIU ON)
set(CMAKE_CROSSCOMPILING ON)

if(NOT DEFINED ENV{DEVKITPPC})
   message(FATAL_ERROR "Please set DEVKITPPC in your environment. export DEVKITPPC=/path/to/devkitPPC")
endif()

if(NOT DEFINED ENV{DEVKITPRO})
   message(FATAL_ERROR "Please set DEVKITPRO in your environment. export DEVKITPRO=/path/to/devkitPRO")
endif()


set(CMAKE_WARN_DEPRECATED OFF)
INCLUDE(CMakeForceCompiler)
CMAKE_FORCE_C_COMPILER($ENV{DEVKITPPC}/bin/powerpc-eabi-gcc GNU)
CMAKE_FORCE_CXX_COMPILER($ENV{DEVKITPPC}/bin/powerpc-eabi-g++ GNU)
set(CMAKE_WARN_DEPRECATED ON)

SET(CMAKE_FIND_ROOT_PATH  $ENV{DEVKITPPC})
SET(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
SET(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
SET(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# specify the cross compiler
#SET(CMAKE_C_COMPILER   $ENV{DEVKITPPC}/bin/powerpc-eabi-gcc)
#SET(CMAKE_CXX_COMPILER $ENV{DEVKITPPC}/bin/powerpc-eabi-g++)
#SET(CMAKE_ASM_COMPILER $ENV{DEVKITPPC}/bin/powerpc-eabi-as)

if(NOT DEFINED WIIU_ROOT)
   set(WIIU_ROOT ${CMAKE_SOURCE_DIR}/ext/wiiu)
endif()

include_directories(${WIIU_ROOT}/include)
link_directories(${WIIU_ROOT})


add_definitions(-Werror=format -Wno-format-truncation -Wno-format-overflow)
add_definitions(-D__wiiu__ -D__powerpc__ -DFD_SETSIZE=32)
add_definitions(-D_POSIX_THREADS)

add_compile_options(-mcpu=750 -meabi -mhard-float)
add_compile_options(-msdata=eabi)
add_compile_options(-ffunction-sections -fdata-sections)
add_compile_options(-ftls-model=global-dynamic)

add_link_options(-Wl,-z,common-page-size=64,-z,max-page-size=64,-z,nocopyreloc)
add_link_options(-Wl,--emit-relocs,--no-tls-optimize,--gc-sections)
add_link_options(-nostartfiles -T ${WIIU_ROOT}/link.ld)

set(CMAKE_ASM_FLAGS "${CMAKE_ASM_FLAGS} ${ARCH_FLAGS} -mregnames -Wa,--sectname-subst")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17")

if(NOT WIIU_LIBRARIES_ADDED)
add_library(wiiu STATIC
   ${WIIU_ROOT}/missing_libc_functions.c
   ${WIIU_ROOT}/exception_handler.c
   ${WIIU_ROOT}/entry.c
   ${WIIU_ROOT}/logger.c
   ${WIIU_ROOT}/memory.c
   ${WIIU_ROOT}/cxx_utils.cpp
   ${WIIU_ROOT}/fs_utils.c
   ${WIIU_ROOT}/sd_fat_devoptab.c
   ${WIIU_ROOT}/gx2_validation.c
   ${WIIU_ROOT}/shader_info.c
   ${WIIU_ROOT}/stubs.S
   ${WIIU_ROOT}/gthr-wup.cpp
   ${WIIU_ROOT}/shader_disasm.cpp
   )
add_library(pthread STATIC ${WIIU_ROOT}/pthread.c)
add_library(fat STATIC
   ${WIIU_ROOT}/libfat/bit_ops.h
   ${WIIU_ROOT}/libfat/cache.c
   ${WIIU_ROOT}/libfat/cache.h
   ${WIIU_ROOT}/libfat/common.h
   ${WIIU_ROOT}/libfat/directory.c
   ${WIIU_ROOT}/libfat/directory.h
   ${WIIU_ROOT}/libfat/disc.c
   ${WIIU_ROOT}/libfat/disc.h
   ${WIIU_ROOT}/libfat/fatdir.c
   ${WIIU_ROOT}/libfat/fatdir.h
   ${WIIU_ROOT}/libfat/fatfile.c
   ${WIIU_ROOT}/libfat/fatfile.h
   ${WIIU_ROOT}/libfat/file_allocation_table.c
   ${WIIU_ROOT}/libfat/file_allocation_table.h
   ${WIIU_ROOT}/libfat/filetime.c
   ${WIIU_ROOT}/libfat/filetime.h
   ${WIIU_ROOT}/libfat/libfat.c
   ${WIIU_ROOT}/libfat/lock.c
   ${WIIU_ROOT}/libfat/lock.h
   ${WIIU_ROOT}/libfat/mem_allocate.h
   ${WIIU_ROOT}/libfat/partition.c
   ${WIIU_ROOT}/libfat/partition.h
   )
add_library(iosuhax STATIC
   ${WIIU_ROOT}/libiosuhax/iosuhax.c
   ${WIIU_ROOT}/libiosuhax/iosuhax_devoptab.c
   ${WIIU_ROOT}/libiosuhax/iosuhax_disc_interface.c
   )
set(WIIU_LIBRARIES_ADDED TRUE)
endif()

set(RPLTOOL "${WIIU_ROOT}/rpltool/rpltool")

macro(add_rpx_target target)
    add_custom_command(TARGET ${target} POST_BUILD
                       COMMAND ${RPLTOOL} ${target} -S -o ${target}.rpx)
endmacro()
