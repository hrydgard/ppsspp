cmake_minimum_required(VERSION 3.8)
project(libpng17)

add_library(png17 STATIC
	pngconf.h
	pngdebug.h
	png.c
	png.h
	pngerror.c
	pngget.c
	pnginfo.h
	pnglibconf.h
	pngmem.c
	pngpread.c
	pngpriv.h
	pngread.c
	pngrio.c
	pngrtran.c
	pngrutil.c
	pngset.c
	pngstruct.h
	pngtest.c
	pngtrans.c
	pngwio.c
	pngwrite.c
	pngwtran.c
	pngwutil.c
)

# Add arm files for ARM processors
if("${CMAKE_SYSTEM_PROCESSOR}" MATCHES "arm(64)?|aarch64")
	set (ARM_FILES arm/arm_init.c
	)

	# Check if it's 32 bit
	if("${CMAKE_SIZEOF_VOID_P}" EQUAL 4)
		list(APPEND ARM_FILES arm/filter_neon.S)
	else()
		list(APPEND ARM_FILES arm/filter_neon_intrinsics.c)
	endif()

	target_sources(png17 PRIVATE ${ARM_FILES})
endif()
