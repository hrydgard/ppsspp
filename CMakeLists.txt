# vim:noexpandtab:
cmake_minimum_required(VERSION 3.6)
project(PPSSPP)

#This is supposed to work but doesn't!
if (NOT ANDROID)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

enable_language(ASM)
list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake/Modules)

add_definitions(-D__STDC_CONSTANT_MACROS)


if(${CMAKE_CXX_COMPILER_ID} STREQUAL "Clang")
  set(CLANG ON)
  message("Clang enabled")
endif()

# Detect CPU from CMAKE configuration. Toolchains should set this up
if(CMAKE_SYSTEM_PROCESSOR)
	if(${CMAKE_SYSTEM_PROCESSOR} MATCHES "^arm")
		set(ARM ON)
		if(${CMAKE_SYSTEM_PROCESSOR} MATCHES "^armv7")
			set(ARMV7 ON)
			# Horrifying workaround for bug in android cmake stuff for asm files
			if (ANDROID)
				set(CMAKE_ASM_FLAGS "${CMAKE_ASM_FLAGS} -target armv7a-none-linux-android")
			endif()
		endif()
	elseif(${CMAKE_SYSTEM_PROCESSOR} MATCHES "^aarch64")
	  set(ARM64 ON)
	  add_definitions(-DARM64)
	elseif(${CMAKE_SYSTEM_PROCESSOR} MATCHES "^amd64" OR ${CMAKE_SYSTEM_PROCESSOR} MATCHES "^x86_64" OR ${CMAKE_SYSTEM_PROCESSOR} MATCHES "^AMD64")
		set(X86 ON)
    set(X86_64 ON)
	elseif(${CMAKE_SYSTEM_PROCESSOR} MATCHES "^x86" OR ${CMAKE_SYSTEM_PROCESSOR} MATCHES "i.86")
		set(X86 ON)
	elseif(${CMAKE_SYSTEM_PROCESSOR} MATCHES "^mips")
		set(MIPS ON)
	else()
    message("Unknown CMAKE_SYSTEM_PROCESSOR: ${CMAKE_SYSTEM_PROCESSOR}")
	endif()
endif()

include(ccache)

# Remove soon?
set(USE_FFMPEG ON)

if(NOT ANDROID AND NOT IOS)
	if(ARM OR SIMULATOR)
		set(USING_EGL ON)
	endif()
endif()

if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
	set(LINUX ON)
	add_definitions(-D__STDC_CONSTANT_MACROS)
endif()

if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
	set(MACOSX ON)
endif()

if(${CMAKE_SYSTEM_NAME} MATCHES "Android")
	set(ANDROID ON)
endif()

# We only support Vulkan on Unix, Android and Windows.
if(ANDROID OR WIN32 OR (UNIX AND NOT APPLE))
  set(VULKAN ON)
else()
  add_definitions(-DNO_VULKAN)
endif()

if(NOT DEFINED HEADLESS)
	set(HEADLESS OFF)
endif()

if(GOLD)
	add_definitions(-DGOLD)
	message("Gold Build")
else()
	message("Normal Build")
endif()

# Doesn't link on some platforms
#if(NOT DEFINED UNITTEST)
#	set(UNITTEST OFF)
#endif()

# User-editable options (go into CMakeCache.txt)
# :: Processors
option(ARMV7 "Set to ON if targeting an ARMv7 processor" ${ARMV7})
option(ARM "Set to ON if targeting an ARM processor" ${ARM})
option(MIPS "Set to ON if targeting a MIPS processor" ${MIPS})
option(X86 "Set to ON if targeting an X86 processor" ${X86})
option(X86_64 "Set to ON if targeting an X86_64 processor" ${X86_64})
# :: Environments
option(USING_EGL "Set to ON if target environment uses EGL" ${USING_EGL})
option(USING_FBDEV "Set to ON if target environment uses fbdev (eg. Pandora)" ${USING_FBDEV})
option(USING_GLES2 "Set to ON if target device uses OpenGL ES 2.0" ${USING_GLES2})
option(RASPBIAN "Set to ON to workaround threading issues when building for ARMV7 on Raspbian" ${RASPBIAN})
# :: Frontends
option(USING_QT_UI "Set to ON if you wish to use the Qt frontend wrapper" ${USING_QT_UI})
option(MOBILE_DEVICE "Set to ON when targeting a mobile device" ${MOBILE_DEVICE})
option(HEADLESS "Set to OFF to not generate the PPSSPPHeadless target" ${HEADLESS})
option(UNITTEST "Set to ON to generate the unittest target" ${UNITTEST})
option(SIMULATOR "Set to ON when targeting an x86 simulator of an ARM platform" ${SIMULATOR})
# :: Options
option(USE_FFMPEG "Build with FFMPEG support" ${USE_FFMPEG})
option(USE_SYSTEM_FFMPEG "Dynamically link against system FFMPEG" ${USE_SYSTEM_FFMPEG})
option(USE_WAYLAND_WSI "Set to ON to require Wayland support for Vulkan" ${USE_WAYLAND_WSI})

if(UNIX AND NOT (APPLE OR ANDROID) AND VULKAN)
	add_definitions(-DVK_USE_PLATFORM_XLIB_KHR)
	# add_definitions(-DVK_USE_PLATFORM_XCB_KHR)
	find_package(Wayland)
	if (USE_WAYLAND_WSI AND NOT WAYLAND_FOUND)
		message(FATAL_ERROR "Could not find libwayland, but USE_WAYLAND_WSI was enabled. Failing.")
	endif()
	if (WAYLAND_FOUND)
		include_directories(${WAYLAND_INCLUDE_DIR})
		add_definitions(-DVK_USE_PLATFORM_WAYLAND_KHR)
	endif()
endif()

if(ANDROID)
	set(CoreLibName ppsspp_jni)
	set(CoreLinkType SHARED)
	set(MOBILE_DEVICE ON)
	set(USING_GLES2 ON)
else()
	set(CoreLibName Core)
	set(CoreLinkType STATIC)
endif()

include_directories(ext/native)

if(NOT OPENGL_LIBRARIES AND USING_GLES2)
	set(OPENGL_LIBRARIES GLESv2)
endif()

if(USING_EGL)
	if(NOT EGL_LIBRARIES)
		set(EGL_LIBRARIES EGL)
	endif()
	set(OPENGL_LIBRARIES ${OPENGL_LIBRARIES} ${EGL_LIBRARIES})
endif()

if(NOT OPENGL_LIBRARIES)
	find_package(OpenGL REQUIRED)
endif()

find_package(SDL2)
include(FindThreads)

if(APPLE)
	find_library(COCOA_LIBRARY Cocoa)
endif()

include_directories("${CMAKE_SOURCE_DIR}")

if(USING_EGL)
	add_definitions(-DUSING_EGL)
endif()
if(USING_FBDEV)
	add_definitions(-DUSING_FBDEV)
endif()
if(USING_GLES2)
	add_definitions(-DUSING_GLES2)
endif()
if(MOBILE_DEVICE)
	add_definitions(-DMOBILE_DEVICE)
endif()

if (NOT CMAKE_BUILD_TYPE)
	message(STATUS "No build type selected, default to Release")
	set(CMAKE_BUILD_TYPE "Release")
endif()

message("CMAKE_SYSTEM_PROCESSOR: ${CMAKE_SYSTEM_PROCESSOR}")

# Let's not use elseif here so we can catch dupes.
if(ARMV7)
	message("Generating for ARMv7, ${CMAKE_BUILD_TYPE}")
endif()
if(ARM)
	message("Generating for ARM, ${CMAKE_BUILD_TYPE}")
endif()
if(MIPS AND X86)
	message("Generating for MIPS in x86 mode, ${CMAKE_BUILD_TYPE}")
endif()
if(MIPS)
	message("Generating for MIPS, ${CMAKE_BUILD_TYPE}")
endif()
if(X86)
	message("Generating for x86, ${CMAKE_BUILD_TYPE}")
endif()
if(X86_64)
	message("Generating for x86_64, ${CMAKE_BUILD_TYPE}")
endif()
if(ARM64)
	message("Generating for ARMv8, ${CMAKE_BUILD_TYPE}")
endif()
if(NOT MSVC)
	if (ANDROID)
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=gnu++11")
	endif()
	if (CLANG)
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-switch -Wno-uninitialized")
	endif()

	set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -D_DEBUG")
	set(CMAKE_CXX_FLAGS_MINSIZEREL "${CMAKE_CXX_FLAGS_MINSIZEREL} -Os -D_NDEBUG")
	set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -D_NDEBUG")
	set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} -O2 -g -D_NDEBUG")
	set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -g -D_DEBUG")
	set(CMAKE_C_FLAGS_MINSIZEREL "${CMAKE_C_FLAGS_MINSIZEREL} -Os -D_NDEBUG")
	set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O3 -D_NDEBUG")
	set(CMAKE_C_FLAGS_RELWITHDEBINFO "${CMAKE_C_FLAGS_RELWITHDEBINFO} -O2 -g -D_NDEBUG")
	#TODO: Remove this and include the file properly everywhere it makes sense
	# First step is too use the macros everywhere
	# Second step is to remove the compatibility defines
	# Third step is to include the file
	# Fourth step is to remove that line!
	add_compile_options(-include ${CMAKE_CURRENT_SOURCE_DIR}/ppsspp_config.h)

	# Disable some warnings
	add_definitions(-Wno-multichar)
	add_definitions(-Wno-deprecated-register)

	# Don't compile with strict aliasing, we're not 100% aliasing-safe
	add_definitions(-fno-strict-aliasing)
	if (${CMAKE_C_COMPILER_ID} STREQUAL "Intel")
		set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -parallel -fopenmp")
	endif()

	if(X86 OR X86_64)
		# enable sse2 code generation
		add_definitions(-msse2)
	endif()

	if(IOS)
	elseif(APPLE AND NOT CMAKE_CROSSCOMPILING)
		# We want C++11, so target 10.7+
		set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mmacosx-version-min=10.7")
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mmacosx-version-min=10.7 -stdlib=libc++ -U__STRICT_ANSI__")
		set(CMAKE_XCODE_ATTRIBUTE_CLANG_CXX_LIBRARY "libc++")
	elseif(NOT ANDROID)
		# TODO: See if we can get rid of no-psabi
		if (NOT ${CMAKE_C_COMPILER_ID} STREQUAL "Intel" AND NOT ${CMAKE_CXX_COMPILER_ID} STREQUAL "Clang")
			add_definitions(-Wno-psabi)
		endif()
		add_definitions(-D_XOPEN_SOURCE=700)
		add_definitions(-D_XOPEN_SOURCE_EXTENDED -D__BSD_VISIBLE=1)
		add_definitions(-D_LARGEFILE64_SOURCE=1 -D_FILE_OFFSET_BITS=64)
	elseif(ANDROID)
		add_definitions(-fsigned-char)
	endif()
else()
	# Disable warnings about MS-specific _s variants of libc functions
	add_definitions(-D_CRT_SECURE_NO_WARNINGS)
	add_definitions(-D_UNICODE -DUNICODE)
	add_definitions(-DUSING_WIN_UI)
	add_definitions(-MP)
	set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -D_DEBUG")
	set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -D_NDEBUG")
endif()

if(NOT ANDROID)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
endif()

# This sets up the MSVC project dirs according to the physical project dirs
macro(setup_target_project TargetName ProjectDir)
	get_property(TargetSources TARGET "${TargetName}" PROPERTY SOURCES)
	foreach(Source ${TargetSources})
		# Figure out the file's path relative to the ProjectDir
		# NOTE: &#$@ double-quoted regexps
		string(REGEX REPLACE "${ProjectDir}" "" RelativePath "${Source}")
		string(REGEX REPLACE "[\\\\/][^\\\\/]*$" "" RelativePath "${RelativePath}")
		string(REGEX REPLACE "^[\\\\/]" "" RelativePath "${RelativePath}")
		string(REGEX REPLACE "/" "\\\\\\\\" RelativePath "${RelativePath}")
		# put the source file in a source_group equivalent to the relative path
		source_group("${RelativePath}" FILES ${Source})
	endforeach()
endmacro()

# Commented-out files are files that don't compile
# and were disabled in the original MSVC project anyway

set(CommonX86
	Common/ABI.cpp
	Common/ABI.h
	Common/CPUDetect.cpp
	Common/CPUDetect.h
	Common/Thunk.cpp
	Common/Thunk.h
	Common/x64Analyzer.cpp
	Common/x64Analyzer.h
	Common/x64Emitter.cpp
	Common/x64Emitter.h)
source_group(x86 FILES ${CommonX86})

set(CommonARM
	Common/ArmCPUDetect.cpp
	Common/ArmEmitter.h
	Common/ArmEmitter.cpp
	Common/ColorConvNEON.cpp)
source_group(ARM FILES ${CommonARM})

set(CommonARM64
	Common/Arm64Emitter.h
	Common/Arm64Emitter.cpp
	Common/ArmEmitter.h
	Common/ArmEmitter.cpp
	Core/Util/DisArm64.cpp)
source_group(ARM64 FILES ${CommonARM64})

set(CommonMIPS
	Common/MipsCPUDetect.cpp
	Common/MipsEmitter.cpp
	Common/MipsEmitter.h)
source_group(MIPS FILES ${CommonMIPS})

if(NOT (X86 OR ARM OR MIPS))
	set(CommonFake
		Common/FakeCPUDetect.cpp
		Common/FakeEmitter.h)
	source_group(Fake FILES ${CommonFake})
endif()

set(CommonWindows
	Common/stdafx.cpp
	Common/stdafx.h)
source_group(Windows FILES ${CommonWindows})

set(CommonVulkan ${CommonExtra}
	Common/Vulkan/SPIRVDisasm.cpp
	Common/Vulkan/SPIRVDisasm.h
	Common/Vulkan/VulkanContext.cpp
	Common/Vulkan/VulkanContext.h
	Common/Vulkan/VulkanDebug.cpp
	Common/Vulkan/VulkanDebug.h
	Common/Vulkan/VulkanImage.cpp
	Common/Vulkan/VulkanImage.h
	Common/Vulkan/VulkanLoader.cpp
	Common/Vulkan/VulkanLoader.h
	Common/Vulkan/VulkanMemory.cpp
	Common/Vulkan/VulkanMemory.h)

add_library(Common STATIC
	${CommonX86}
	${CommonARM}
	${CommonARM64}
	${CommonMIPS}
	${CommonFake}
	${CommonWindows}
	${CommonVulkan}
	Common/ColorConv.cpp
	Common/ColorConv.h
	Common/ChunkFile.cpp
	Common/ChunkFile.h
	Common/ConsoleListener.cpp
	Common/ConsoleListener.h
	Common/Crypto/md5.cpp
	Common/Crypto/md5.h
	Common/Crypto/sha1.cpp
	Common/Crypto/sha1.h
	Common/Crypto/sha256.cpp
	Common/Crypto/sha256.h
	Common/FileUtil.cpp
	Common/FileUtil.h
	Common/KeyMap.cpp
	Common/KeyMap.h
	Common/LogManager.cpp
	Common/Hashmaps.h
	Common/LogManager.h
	Common/MemArenaAndroid.cpp
	Common/MemArenaDarwin.cpp
	Common/MemArenaPosix.cpp
	Common/MemArenaWin32.cpp
	Common/MemArena.h
	Common/MemoryUtil.cpp
	Common/MemoryUtil.h
	Common/Misc.cpp
	Common/MsgHandler.cpp
	Common/MsgHandler.h
	Common/OSVersion.cpp
	Common/OSVersion.h
	Common/StringUtils.cpp
	Common/StringUtils.h
	Common/ThreadPools.cpp
	Common/ThreadPools.h
	Common/ThreadSafeList.h
	Common/Timer.cpp
	Common/Timer.h)

add_subdirectory(ext)

include_directories(Common)
setup_target_project(Common Common)

target_link_libraries(Common Ext::Snappy)

if(WIN32)
	include_directories(dx9sdk/Include)
	target_link_libraries(Common winmm d3d9 dxguid dsound)
endif()

if(TARGET SDL2::SDL2)
	target_link_libraries(Common SDL2::SDL2)
endif()

if(USING_GLES2)
	find_package(X11)
endif()

add_library(vjson STATIC
	ext/native/ext/vjson/json.cpp
	ext/native/ext/vjson/json.h
	ext/native/ext/vjson/block_allocator.cpp
	ext/native/ext/vjson/block_allocator.h
)

add_library(rg_etc1 STATIC
	ext/native/ext/rg_etc1/rg_etc1.cpp
	ext/native/ext/rg_etc1/rg_etc1.h)
include_directories(ext/native/ext/rg_etc1)

if(USE_FFMPEG)
  if(NOT FFMPEG_DIR)
    if(NOT USE_SYSTEM_FFMPEG)
      if(ANDROID)
        if(ARMV7)
          set(PLATFORM_ARCH "android/armv7")
        elseif(ARM64)
          set(PLATFORM_ARCH "android/arm64")
        elseif(X86_64)
          set(PLATFORM_ARCH "android/x86_64")
        elseif(X86)
          set(PLATFORM_ARCH "android/x86")
        endif()
      elseif(IOS)
        set(PLATFORM_ARCH "ios/universal")
      elseif(MACOSX)
        set(PLATFORM_ARCH "macosx/x86_64")
      elseif(LINUX)
        if(ARMV7)
          set(PLATFORM_ARCH "linux/armv7")
        elseif(ARM64)
          set(PLATFORM_ARCH "linux/aarch64")
        elseif(ARM)
          set(PLATFORM_ARCH "linux/arm")
        elseif(MIPS)
          set(PLATFORM_ARCH "linux/mips32")
        elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
          set(PLATFORM_ARCH "linux/x86_64")
        elseif(X86)
          set(PLATFORM_ARCH "linux/x86")
        endif()
      elseif(WIN32)
        if(X86_64)
          set(PLATFORM_ARCH "Windows/x86_64")
        elseif(X86)
          set(PLATFORM_ARCH "Windows/x86")
        endif()
      endif()
      if(PLATFORM_ARCH)
        set(FFMPEG_DIR "ffmpeg/${PLATFORM_ARCH}")
      else()
        message("Couldn't find an internal FFmpeg build, using system FFmpeg instead")
      endif()
    endif()
  endif()

  find_package(FFmpeg REQUIRED avcodec avformat avutil swresample swscale)
endif(USE_FFMPEG)

find_package(ZLIB)
if(ZLIB_FOUND AND NOT ANDROID)
	include_directories(${ZLIB_INCLUDE_DIR})
	add_definitions(-DSHARED_ZLIB)
else()
	add_library(zlib STATIC
		ext/zlib/adler32.c
		ext/zlib/compress.c
		ext/zlib/crc32.c
		ext/zlib/crc32.h
		ext/zlib/deflate.c
		ext/zlib/deflate.h
		ext/zlib/gzclose.c
		ext/zlib/gzguts.h
		ext/zlib/gzlib.c
		ext/zlib/gzread.c
		ext/zlib/gzwrite.c
		ext/zlib/infback.c
		ext/zlib/inffast.c
		ext/zlib/inffast.h
		ext/zlib/inffixed.h
		ext/zlib/inflate.c
		ext/zlib/inflate.h
		ext/zlib/inftrees.c
		ext/zlib/inftrees.h
		ext/zlib/make_vms.com
		ext/zlib/trees.c
		ext/zlib/trees.h
		ext/zlib/uncompr.c
		ext/zlib/zconf.h
		ext/zlib/zlib.h
		ext/zlib/zutil.c
		ext/zlib/zutil.h
	)
	include_directories(ext/zlib)
	set(ZLIB_LIBRARY zlib)
endif()

add_library(cityhash STATIC
	ext/native/ext/cityhash/city.cpp
	ext/native/ext/cityhash/city.h
	ext/native/ext/cityhash/citycrc.h
)
include_directories(ext/cityhash)

if (NOT MSVC)
	# These can be fast even for debug.
	set_target_properties(udis86 PROPERTIES COMPILE_FLAGS "-O3")
	set_target_properties(cityhash PROPERTIES COMPILE_FLAGS "-O3")
	if(NOT ZLIB_FOUND)
		set_target_properties(zlib PROPERTIES COMPILE_FLAGS "-O3")
	endif()
endif()


find_package(LibZip)
if(LIBZIP_FOUND)
	add_definitions(-DSHARED_LIBZIP)
else()
	add_library(libzip STATIC
		ext/native/ext/libzip/zip.h
		ext/native/ext/libzip/mkstemp.c
		ext/native/ext/libzip/zip_add.c
		ext/native/ext/libzip/zip_add_dir.c
		ext/native/ext/libzip/zip_close.c
		ext/native/ext/libzip/zip_delete.c
		ext/native/ext/libzip/zip_dirent.c
		ext/native/ext/libzip/zip_entry_free.c
		ext/native/ext/libzip/zip_entry_new.c
		ext/native/ext/libzip/zip_err_str.c
		ext/native/ext/libzip/zip_error.c
		ext/native/ext/libzip/zip_error_clear.c
		ext/native/ext/libzip/zip_error_get.c
		ext/native/ext/libzip/zip_error_get_sys_type.c
		ext/native/ext/libzip/zip_error_strerror.c
		ext/native/ext/libzip/zip_error_to_str.c
		ext/native/ext/libzip/zip_fclose.c
		ext/native/ext/libzip/zip_file_error_clear.c
		ext/native/ext/libzip/zip_file_error_get.c
		ext/native/ext/libzip/zip_file_get_offset.c
		ext/native/ext/libzip/zip_file_strerror.c
		ext/native/ext/libzip/zip_filerange_crc.c
		ext/native/ext/libzip/zip_fopen.c
		ext/native/ext/libzip/zip_fopen_index.c
		ext/native/ext/libzip/zip_fread.c
		ext/native/ext/libzip/zip_free.c
		ext/native/ext/libzip/zip_get_archive_comment.c
		ext/native/ext/libzip/zip_get_archive_flag.c
		ext/native/ext/libzip/zip_get_file_comment.c
		ext/native/ext/libzip/zip_get_name.c
		ext/native/ext/libzip/zip_get_num_files.c
		ext/native/ext/libzip/zip_memdup.c
		ext/native/ext/libzip/zip_name_locate.c
		ext/native/ext/libzip/zip_new.c
		ext/native/ext/libzip/zip_open.c
		ext/native/ext/libzip/zip_rename.c
		ext/native/ext/libzip/zip_replace.c
		ext/native/ext/libzip/zip_set_archive_comment.c
		ext/native/ext/libzip/zip_set_archive_flag.c
		ext/native/ext/libzip/zip_set_file_comment.c
		ext/native/ext/libzip/zip_set_name.c
		ext/native/ext/libzip/zip_source_buffer.c
		ext/native/ext/libzip/zip_source_file.c
		ext/native/ext/libzip/zip_source_filep.c
		ext/native/ext/libzip/zip_source_free.c
		ext/native/ext/libzip/zip_source_function.c
		ext/native/ext/libzip/zip_source_zip.c
		ext/native/ext/libzip/zip_stat.c
		ext/native/ext/libzip/zip_stat_index.c
		ext/native/ext/libzip/zip_stat_init.c
		ext/native/ext/libzip/zip_strerror.c
		ext/native/ext/libzip/zip_unchange.c
		ext/native/ext/libzip/zip_unchange_all.c
		ext/native/ext/libzip/zip_unchange_archive.c
		ext/native/ext/libzip/zip_unchange_data.c)
	target_link_libraries(libzip)
	include_directories(ext/native/ext/libzip)
	set(LIBZIP_LIBRARY libzip)
endif()

# FindPNG does a few things we don't want. So do it ourselves. Fixed to libpng17
find_path(PNG_PNG_INCLUDE_DIR NAMES "libpng17/png.h")
find_library(PNG_LIBRARY NAMES png17 libpng17)
find_package(PackageHandleStandardArgs)
find_package_handle_standard_args(PNG REQUIRED_VARS PNG_LIBRARY PNG_PNG_INCLUDE_DIR)
if (PNG_FOUND)
	include_directories(${PNG_PNG_INCLUDE_DIR})
else()
	if(ARM)
		set(PNG_ARM_INCLUDES
            ext/native/ext/libpng17/arm/arm_init.c
            ext/native/ext/libpng17/arm/filter_neon.S
            ext/native/ext/libpng17/arm/filter_neon_intrinsics.c
        )
	endif()
	add_library(png17 STATIC
		ext/native/ext/libpng17/pngconf.h
		ext/native/ext/libpng17/pngdebug.h
		ext/native/ext/libpng17/png.c
		ext/native/ext/libpng17/png.h
		ext/native/ext/libpng17/pngerror.c
		ext/native/ext/libpng17/pngget.c
		ext/native/ext/libpng17/pnginfo.h
		ext/native/ext/libpng17/pnglibconf.h
		ext/native/ext/libpng17/pngmem.c
		ext/native/ext/libpng17/pngpread.c
		ext/native/ext/libpng17/pngpriv.h
		ext/native/ext/libpng17/pngread.c
		ext/native/ext/libpng17/pngrio.c
		ext/native/ext/libpng17/pngrtran.c
		ext/native/ext/libpng17/pngrutil.c
		ext/native/ext/libpng17/pngset.c
		ext/native/ext/libpng17/pngstruct.h
		ext/native/ext/libpng17/pngtrans.c
		ext/native/ext/libpng17/pngwio.c
		ext/native/ext/libpng17/pngwrite.c
		ext/native/ext/libpng17/pngwtran.c
		ext/native/ext/libpng17/pngwutil.c
		${PNG_ARM_INCLUDES} )
	set(PNG_LIBRARY png17)
	include_directories(ext/native/ext)
endif()

set(nativeExtra)
set(nativeExtraLibs)

if(ANDROID)
  set(nativeExtra ${nativeExtra}
    Common/GL/GLInterface/EGLAndroid.cpp
    Common/GL/GLInterface/EGLAndroid.h
    Common/GL/GLInterface/EGL.cpp
    Common/GL/GLInterface/EGL.h
    Common/GL/GLInterface/GLInterface.cpp
    Common/GL/GLInterfaceBase.h)

	set(nativeExtra ${nativeExtra}
		ext/native/base/NativeApp.h
		android/jni/app-android.cpp
		android/jni/native_audio.cpp
		android/jni/native_audio.h
		android/jni/native-audio-so.cpp
		android/jni/native-audio-so.h)
	# No target
elseif(IOS)
	set(nativeExtra ${nativeExtra}
		ios/main.mm
		ios/AppDelegate.mm
		ios/AppDelegate.h
		ios/ViewController.mm
		ios/ViewController.h
		ios/iOSCoreAudio.mm
		ios/iOSCoreAudio.h
		ios/PPSSPPUIApplication.h
		ios/PPSSPPUIApplication.mm
		ios/SmartKeyboardMap.cpp
		ios/SmartKeyboardMap.hpp
		ios/iCade/iCadeReaderView.h
		ios/iCade/iCadeReaderView.m
		ios/iCade/iCadeState.h)
	set(nativeExtraLibs ${nativeExtraLibs} "-framework Foundation -framework AudioToolbox -framework CoreGraphics -framework QuartzCore -framework UIKit -framework GLKit -framework OpenAL -framework AVFoundation")
	if(EXISTS "${CMAKE_IOS_SDK_ROOT}/System/Library/Frameworks/GameController.framework")
		set(nativeExtraLibs ${nativeExtraLibs} "-weak_framework GameController")
	endif()

	if(NOT ICONV_LIBRARY)
		set(nativeExtraLibs ${nativeExtraLibs} iconv)
	endif()

	set_source_files_properties(ios/AppDelegate.mm PROPERTIES COMPILE_FLAGS -fobjc-arc)
	set_source_files_properties(ios/ViewController.mm PROPERTIES COMPILE_FLAGS -fobjc-arc)
	set_source_files_properties(ios/iOSCoreAudio.mm PROPERTIES COMPILE_FLAGS -fobjc-arc)
	set_source_files_properties(ios/PPSSPPUIApplication.mm PROPERTIES COMPILE_FLAGS -fobjc-arc)
	set_source_files_properties(ios/iCade/iCadeReaderView.m PROPERTIES COMPILE_FLAGS -fobjc-arc)
	set_source_files_properties(ios/main.mm PROPERTIES COMPILE_FLAGS -fobjc-arc)


	set(TargetBin PPSSPP)
elseif(USING_QT_UI)
    set(CMAKE_AUTOMOC ON)
    find_package(Qt5 COMPONENTS OpenGL Gui Core)
    if(NOT SDL2_FOUND)
        find_package(Qt5 COMPONENTS Multimedia)
    endif(NOT SDL2_FOUND)
    set(Qt_UI
      Qt/Debugger/debugger_disasm.ui
      Qt/Debugger/debugger_displaylist.ui
      Qt/Debugger/debugger_memory.ui
      Qt/Debugger/debugger_memorytex.ui
      Qt/Debugger/debugger_vfpu.ui
    )
    qt5_wrap_ui(QT_UI_GEN ${Qt_UI})
    list(APPEND NativeAppSource
      ${QT_UI_GEN}
      Qt/QtMain.cpp
      Qt/QtMain.h
      Qt/mainwindow.cpp
      Qt/mainwindow.h
      Qt/Debugger/ctrldisasmview.cpp
      Qt/Debugger/ctrldisasmview.h
      Qt/Debugger/ctrlmemview.cpp
      Qt/Debugger/ctrlmemview.h
      Qt/Debugger/ctrlregisterlist.cpp
      Qt/Debugger/ctrlregisterlist.h
      Qt/Debugger/ctrlvfpuview.cpp
      Qt/Debugger/ctrlvfpuview.h
      Qt/Debugger/debugger_disasm.cpp
      Qt/Debugger/debugger_disasm.h
      Qt/Debugger/debugger_displaylist.cpp
      Qt/Debugger/debugger_displaylist.h
      Qt/Debugger/debugger_memory.cpp
      Qt/Debugger/debugger_memory.h
      Qt/Debugger/debugger_memorytex.cpp
      Qt/Debugger/debugger_memorytex.h
      Qt/Debugger/debugger_vfpu.cpp
      Qt/Debugger/debugger_vfpu.h
    )
    add_definitions(-DUSING_QT_UI)
    include_directories(${CMAKE_CURRENT_BINARY_DIR} Qt Qt/Debugger)
	set(nativeExtraLibs ${nativeExtraLibs} Qt5::OpenGL Qt5::Gui Qt5::Core)
	set(TargetBin PPSSPPQt)

    # Enable SDL if found
    if (SDL2_FOUND)
        add_definitions(-DSDL)
        set(nativeExtra ${nativeExtra}
            SDL/SDLJoystick.h
            SDL/SDLJoystick.cpp)
        set(nativeExtraLibs ${nativeExtraLibs} SDL2::SDL2)
    else(SDL2_FOUND)
        set(nativeExtraLibs ${nativeExtraLibs} Qt5::Multimedia)
    endif()

elseif(TARGET SDL2::SDL2)
	set(TargetBin PPSSPPSDL)
	# Require SDL
	add_definitions(-DSDL)
	set(nativeExtra ${nativeExtra}
		SDL/SDLJoystick.h
		SDL/SDLJoystick.cpp
		SDL/SDLMain.cpp)
	set(nativeExtraLibs ${nativeExtraLibs} SDL2::SDL2)
	if(APPLE)
		set(nativeExtra ${nativeExtra} SDL/SDLMain.h SDL/SDLMain.mm)
		set(nativeExtraLibs ${nativeExtraLibs} ${COCOA_LIBRARY})
	elseif(USING_EGL)
		set(nativeExtraLibs ${nativeExtraLibs} pthread)
	endif()
	set(TargetBin PPSSPPSDL)
elseif(WIN32)
	# Don't care about SDL.
	set(TargetBin PPSSPPWindows)
	if(X86_64)
		link_directories(dx9sdk/Lib/x64)
	else()
		link_directories(dx9sdk/Lib/x86)
	endif()
else()
	message(FATAL_ERROR "Could not find SDL2. Failing.")
endif()

list(APPEND NativeAppSource
	android/jni/TestRunner.cpp
	UI/NativeApp.cpp
	UI/BackgroundAudio.cpp
	UI/DevScreens.cpp
	UI/DisplayLayoutEditor.cpp
	UI/DisplayLayoutScreen.cpp
	UI/EmuScreen.cpp
	UI/GameInfoCache.cpp
	UI/MainScreen.cpp
	UI/MiscScreens.cpp
	UI/PauseScreen.cpp
	UI/GameScreen.cpp
	UI/GameSettingsScreen.cpp
	UI/TiltAnalogSettingsScreen.cpp
	UI/TiltEventProcessor.cpp
	UI/TouchControlLayoutScreen.cpp
	UI/TouchControlVisibilityScreen.cpp
	UI/GamepadEmu.cpp
	UI/OnScreenDisplay.cpp
	UI/ControlMappingScreen.cpp
	UI/RemoteISOScreen.cpp
	UI/ReportScreen.cpp
	UI/SavedataScreen.cpp
	UI/Store.cpp
	UI/CwCheatScreen.cpp
	UI/InstallZipScreen.cpp
	UI/ProfilerDraw.cpp
	UI/ui_atlas.cpp
	UI/TextureUtil.cpp
	UI/ComboKeyMappingScreen.cpp)

if(ANDROID)
	if (ARM)
		set(NativeAppSource ${NativeAppSource} android/jni/ArmEmitterTest.cpp)
  elseif (ARM64)
		set(NativeAppSource ${NativeAppSource} android/jni/Arm64EmitterTest.cpp)
	endif()
	set(nativeExtra ${nativeExtra} ${NativeAppSource})
endif()

set(THIN3D_PLATFORMS ext/native/thin3d/thin3d_gl.cpp)
set(THIN3D_PLATFORMS ${THIN3D_PLATFORMS}
	ext/native/thin3d/thin3d_vulkan.cpp
	ext/native/thin3d/VulkanRenderManager.cpp
	ext/native/thin3d/VulkanRenderManager.h
	ext/native/thin3d/VulkanQueueRunner.cpp
	ext/native/thin3d/VulkanQueueRunner.h
	ext/native/thin3d/DataFormat.h)

if(WIN32)
  set(THIN3D_PLATFORMS ${THIN3D_PLATFORMS}
    ext/native/thin3d/thin3d_d3d9.cpp
    ext/native/thin3d/thin3d_d3d11.cpp
    ext/native/thin3d/d3dx9_loader.cpp
    ext/native/thin3d/d3dx9_loader.h
    ext/native/thin3d/d3d11_loader.cpp
    ext/native/thin3d/d3d11_loader.h)
endif()

add_library(native STATIC
	${nativeExtra}
	ext/native/base/backtrace.cpp
	ext/native/base/backtrace.h
	ext/native/base/basictypes.h
	ext/native/base/buffer.cpp
	ext/native/base/buffer.h
	ext/native/base/colorutil.cpp
	ext/native/base/colorutil.h
	ext/native/base/display.cpp
	ext/native/base/display.h
	ext/native/base/linked_ptr.h
	ext/native/base/logging.h
	ext/native/base/stringutil.cpp
	ext/native/base/stringutil.h
	ext/native/base/timeutil.cpp
	ext/native/base/timeutil.h
	ext/native/data/compression.cpp
	ext/native/data/compression.h
	ext/native/ext/vjson/json.cpp
	ext/native/ext/vjson/json.h
	ext/native/ext/vjson/block_allocator.cpp
	ext/native/ext/vjson/block_allocator.h
	ext/native/file/chunk_file.cpp
	ext/native/file/chunk_file.h
	ext/native/file/fd_util.cpp
	ext/native/file/fd_util.h
	ext/native/file/file_util.cpp
	ext/native/file/file_util.h
	ext/native/file/free.cpp
	ext/native/file/free.h
	ext/native/file/ini_file.cpp
	ext/native/file/ini_file.h
	ext/native/file/path.cpp
	ext/native/file/path.h
	ext/native/file/vfs.h
	ext/native/file/zip_read.cpp
	ext/native/file/zip_read.h
	ext/native/gfx/gl_common.h
	ext/native/gfx/gl_debug_log.cpp
	ext/native/gfx/gl_debug_log.h
	ext/native/gfx/texture_atlas.cpp
	ext/native/gfx/texture_atlas.h
	ext/native/gfx/d3d9_shader.cpp
	ext/native/gfx/d3d9_shader.h
	ext/native/gfx/d3d9_state.cpp
	ext/native/gfx/d3d9_state.h
	ext/native/gfx_es2/draw_buffer.cpp
	ext/native/gfx_es2/draw_buffer.h
	ext/native/gfx_es2/draw_text.cpp
	ext/native/gfx_es2/draw_text.h
	ext/native/gfx_es2/draw_text_win.cpp
	ext/native/gfx_es2/draw_text_win.h
	ext/native/gfx_es2/draw_text_qt.cpp
	ext/native/gfx_es2/draw_text_qt.h
	ext/native/gfx_es2/draw_text_android.cpp
	ext/native/gfx_es2/draw_text_android.h
	ext/native/gfx_es2/gpu_features.cpp
	ext/native/gfx_es2/gpu_features.h
	ext/native/gfx_es2/glsl_program.cpp
	ext/native/gfx_es2/glsl_program.h
	ext/native/gfx_es2/gl3stub.c
	ext/native/gfx_es2/gl3stub.h
	ext/native/i18n/i18n.cpp
	ext/native/i18n/i18n.h
	ext/native/image/png_load.cpp
	ext/native/image/png_load.h
	ext/native/image/zim_load.cpp
	ext/native/image/zim_load.h
	ext/native/image/zim_save.cpp
	ext/native/image/zim_save.h
	ext/native/input/gesture_detector.cpp
	ext/native/input/gesture_detector.h
	ext/native/input/keycodes.h
	ext/native/input/input_state.h
	ext/native/input/input_state.cpp
	ext/native/math/fast/fast_math.c
	ext/native/math/fast/fast_matrix.c
	ext/native/math/fast/fast_matrix_neon.S
	ext/native/math/fast/fast_matrix_sse.c
	ext/native/math/dataconv.cpp
	ext/native/math/dataconv.h
	ext/native/math/curves.cpp
	ext/native/math/curves.h
	ext/native/math/expression_parser.cpp
	ext/native/math/expression_parser.h
	ext/native/math/lin/matrix4x4.cpp
	ext/native/math/lin/matrix4x4.h
	ext/native/math/lin/plane.cpp
	ext/native/math/lin/plane.h
	ext/native/math/lin/quat.cpp
	ext/native/math/lin/quat.h
	ext/native/math/lin/ray.h
	ext/native/math/lin/vec3.cpp
	ext/native/math/lin/vec3.h
	ext/native/math/math_util.cpp
	ext/native/math/math_util.h
	ext/native/net/http_client.cpp
	ext/native/net/http_client.h
	ext/native/net/http_headers.cpp
	ext/native/net/http_headers.h
	ext/native/net/http_server.cpp
	ext/native/net/http_server.h
	ext/native/net/resolve.cpp
	ext/native/net/resolve.h
	ext/native/net/sinks.cpp
	ext/native/net/sinks.h
	ext/native/net/url.cpp
	ext/native/net/url.h
	ext/native/profiler/profiler.cpp
	ext/native/profiler/profiler.h
	ext/native/thin3d/thin3d.cpp
	ext/native/thin3d/thin3d.h
	${THIN3D_PLATFORMS}
	ext/native/thread/executor.cpp
	ext/native/thread/executor.h
	ext/native/thread/prioritizedworkqueue.cpp
	ext/native/thread/prioritizedworkqueue.h
	ext/native/thread/threadutil.cpp
	ext/native/thread/threadutil.h
	ext/native/thread/threadpool.cpp
	ext/native/thread/threadpool.h
	ext/native/ui/screen.cpp
	ext/native/ui/screen.h
	ext/native/ui/ui.cpp
	ext/native/ui/ui.h
	ext/native/ui/ui_context.cpp
	ext/native/ui/ui_context.h
	ext/native/ui/ui_screen.cpp
	ext/native/ui/ui_screen.h
	ext/native/ui/ui_tween.cpp
	ext/native/ui/ui_tween.h
	ext/native/ui/view.cpp
	ext/native/ui/view.h
	ext/native/ui/viewgroup.cpp
	ext/native/ui/viewgroup.h
	ext/native/util/hash/hash.cpp
	ext/native/util/hash/hash.h
	ext/native/util/random/rng.h
	ext/native/util/text/utf8.h
	ext/native/util/text/utf8.cpp
	ext/native/util/text/parsers.h
	ext/native/util/text/parsers.cpp
	ext/native/util/text/wrap_text.h
	ext/native/util/text/wrap_text.cpp
	ext/native/util/const_map.h
	ext/native/ext/jpge/jpgd.cpp
	ext/native/ext/jpge/jpgd.h
	ext/native/ext/jpge/jpge.cpp
	ext/native/ext/jpge/jpge.h)
include_directories(ext/native)

if (LINUX AND NOT ANDROID)
	SET(RT_LIB rt)
endif()

SET(ATOMIC_LIB)
if(ANDROID)
	SET(ATOMIC_LIB atomic)
endif()

target_link_libraries(native ${LIBZIP_LIBRARY} ${ZLIB_LIBRARY} ${PNG_LIBRARY} rg_etc1 vjson udis86 ${RT_LIB} ${nativeExtraLibs} ${ATOMIC_LIB})
if(TARGET Ext::GLEW)
  target_link_libraries(native Ext::GLEW)
endif()

if(ANDROID)
	target_link_libraries(native log EGL OpenSLES)
elseif(WIN32)
	target_link_libraries(native ws2_32 winmm)
endif()
setup_target_project(native native)

add_library(kirk STATIC
	ext/libkirk/AES.c
	ext/libkirk/AES.h
	ext/libkirk/amctrl.c
	ext/libkirk/amctrl.h
	ext/libkirk/SHA1.c
	ext/libkirk/SHA1.h
	ext/libkirk/bn.c
	ext/libkirk/ec.c
	ext/libkirk/kirk_engine.c
	ext/libkirk/kirk_engine.h)
include_directories(ext/libkirk)

add_library(sfmt19937 STATIC
	ext/sfmt19937/SFMT.c
	ext/sfmt19937/SFMT.h
	ext/sfmt19937/SFMT-common.h
	ext/sfmt19937/SFMT-params.h
	ext/sfmt19937/SFMT-params19937.h
)
include_directories(ext/sfmt19937)

add_library(xbrz STATIC
	ext/xbrz/xbrz.cpp
	ext/xbrz/xbrz.h
)
include_directories(ext/xbrz)

add_library(xxhash STATIC
	ext/xxhash.c
	ext/xxhash.h
)
include_directories(ext/xxhash)

set(CoreExtra)
set(CoreExtraLibs)

if(WINDOWS OR ANDROID)
	set(CoreExtraLibs ${CoreExtraLibs} armips)
endif()

set(CoreExtra ${CoreExtra}
	Core/MIPS/IR/IRCompALU.cpp
	Core/MIPS/IR/IRCompBranch.cpp
	Core/MIPS/IR/IRCompFPU.cpp
	Core/MIPS/IR/IRCompLoadStore.cpp
	Core/MIPS/IR/IRCompVFPU.cpp
	Core/MIPS/IR/IRFrontend.cpp
	Core/MIPS/IR/IRFrontend.h
	Core/MIPS/IR/IRInst.cpp
	Core/MIPS/IR/IRInst.h
	Core/MIPS/IR/IRInterpreter.cpp
	Core/MIPS/IR/IRInterpreter.h
	Core/MIPS/IR/IRJit.cpp
	Core/MIPS/IR/IRJit.h
	Core/MIPS/IR/IRPassSimplify.cpp
	Core/MIPS/IR/IRPassSimplify.h
	Core/MIPS/IR/IRRegCache.cpp
	Core/MIPS/IR/IRRegCache.h
	)

list(APPEND CoreExtra
	Core/MIPS/ARM/ArmAsm.cpp
	Core/MIPS/ARM/ArmCompALU.cpp
	Core/MIPS/ARM/ArmCompBranch.cpp
	Core/MIPS/ARM/ArmCompFPU.cpp
	Core/MIPS/ARM/ArmCompLoadStore.cpp
	Core/MIPS/ARM/ArmCompVFPU.cpp
	Core/MIPS/ARM/ArmCompVFPUNEON.cpp
	Core/MIPS/ARM/ArmCompVFPUNEONUtil.cpp
	Core/MIPS/ARM/ArmCompReplace.cpp
	Core/MIPS/ARM/ArmJit.cpp
	Core/MIPS/ARM/ArmJit.h
	Core/MIPS/ARM/ArmRegCache.cpp
	Core/MIPS/ARM/ArmRegCache.h
	Core/MIPS/ARM/ArmRegCacheFPU.cpp
	Core/MIPS/ARM/ArmRegCacheFPU.h
	GPU/Common/VertexDecoderArm.cpp
)

list(APPEND CoreExtra
	Core/MIPS/ARM64/Arm64Asm.cpp
	Core/MIPS/ARM64/Arm64CompALU.cpp
	Core/MIPS/ARM64/Arm64CompBranch.cpp
	Core/MIPS/ARM64/Arm64CompFPU.cpp
	Core/MIPS/ARM64/Arm64CompLoadStore.cpp
	Core/MIPS/ARM64/Arm64CompVFPU.cpp
	Core/MIPS/ARM64/Arm64CompReplace.cpp
	Core/MIPS/ARM64/Arm64Jit.cpp
	Core/MIPS/ARM64/Arm64Jit.h
	Core/MIPS/ARM64/Arm64RegCache.cpp
	Core/MIPS/ARM64/Arm64RegCache.h
	Core/MIPS/ARM64/Arm64RegCacheFPU.cpp
	Core/MIPS/ARM64/Arm64RegCacheFPU.h
	GPU/Common/VertexDecoderArm64.cpp
	Core/Util/DisArm64.cpp)

list(APPEND CoreExtra
	Core/MIPS/x86/Asm.cpp
	Core/MIPS/x86/CompALU.cpp
	Core/MIPS/x86/CompBranch.cpp
	Core/MIPS/x86/CompFPU.cpp
	Core/MIPS/x86/CompLoadStore.cpp
	Core/MIPS/x86/CompVFPU.cpp
	Core/MIPS/x86/CompReplace.cpp
	Core/MIPS/x86/Jit.cpp
	Core/MIPS/x86/Jit.h
	Core/MIPS/x86/JitSafeMem.cpp
	Core/MIPS/x86/JitSafeMem.h
	Core/MIPS/x86/RegCache.cpp
	Core/MIPS/x86/RegCache.h
	Core/MIPS/x86/RegCacheFPU.cpp
	Core/MIPS/x86/RegCacheFPU.h
	GPU/Common/VertexDecoderX86.cpp
	GPU/Software/SamplerX86.cpp
)

list(APPEND CoreExtra
	Core/MIPS/MIPS/MipsJit.cpp
	Core/MIPS/MIPS/MipsJit.h
	GPU/Common/VertexDecoderFake.cpp
)

if (NOT MOBILE_DEVICE)
	set(CoreExtra ${CoreExtra}
		Core/AVIDump.cpp
		Core/AVIDump.h
		Core/WaveFile.cpp
		Core/WaveFile.h)
endif()

if(ARMV7)
	set(CORE_NEON Core/Util/AudioFormatNEON.cpp Core/Util/AudioFormatNEON.h)
endif()

set(GPU_GLES
	GPU/GLES/DepalettizeShaderGLES.cpp
	GPU/GLES/DepalettizeShaderGLES.h
	GPU/GLES/GPU_GLES.cpp
	GPU/GLES/GPU_GLES.h
	GPU/GLES/FragmentShaderGeneratorGLES.cpp
	GPU/GLES/FragmentShaderGeneratorGLES.h
	GPU/GLES/FragmentTestCacheGLES.cpp
	GPU/GLES/FragmentTestCacheGLES.h
	GPU/GLES/FramebufferManagerGLES.cpp
	GPU/GLES/FramebufferManagerGLES.h
	GPU/GLES/ShaderManagerGLES.cpp
	GPU/GLES/ShaderManagerGLES.h
	GPU/GLES/StateMappingGLES.cpp
	GPU/GLES/StateMappingGLES.h
	GPU/GLES/StencilBufferGLES.cpp
	GPU/GLES/TextureCacheGLES.cpp
	GPU/GLES/TextureCacheGLES.h
	GPU/GLES/TextureScalerGLES.cpp
	GPU/GLES/TextureScalerGLES.h
	GPU/GLES/DrawEngineGLES.cpp
	GPU/GLES/DrawEngineGLES.h
	GPU/GLES/VertexShaderGeneratorGLES.cpp
	GPU/GLES/VertexShaderGeneratorGLES.h
)

set(GPU_VULKAN
  GPU/Vulkan/DepalettizeShaderVulkan.cpp
	GPU/Vulkan/DepalettizeShaderVulkan.h
	GPU/Vulkan/DrawEngineVulkan.cpp
	GPU/Vulkan/DrawEngineVulkan.h
	GPU/Vulkan/FragmentShaderGeneratorVulkan.cpp
	GPU/Vulkan/FragmentShaderGeneratorVulkan.h
	GPU/Vulkan/FramebufferVulkan.cpp
	GPU/Vulkan/FramebufferVulkan.h
	GPU/Vulkan/GPU_Vulkan.cpp
	GPU/Vulkan/GPU_Vulkan.h
	GPU/Vulkan/PipelineManagerVulkan.cpp
	GPU/Vulkan/PipelineManagerVulkan.h
	GPU/Vulkan/ShaderManagerVulkan.cpp
	GPU/Vulkan/ShaderManagerVulkan.h
	GPU/Vulkan/StateMappingVulkan.cpp
	GPU/Vulkan/StateMappingVulkan.h
	GPU/Vulkan/StencilBufferVulkan.cpp
	GPU/Vulkan/TextureCacheVulkan.cpp
	GPU/Vulkan/TextureCacheVulkan.h
	GPU/Vulkan/TextureScalerVulkan.cpp
	GPU/Vulkan/TextureScalerVulkan.h
	GPU/Vulkan/VertexShaderGeneratorVulkan.cpp
	GPU/Vulkan/VertexShaderGeneratorVulkan.h
	GPU/Vulkan/VulkanUtil.cpp
	GPU/Vulkan/VulkanUtil.h
)

set(GPU_D3D9
  GPU/Directx9/DepalettizeShaderDX9.cpp
	GPU/Directx9/DepalettizeShaderDX9.h
	GPU/Directx9/DrawEngineDX9.cpp
	GPU/Directx9/DrawEngineDX9.h
	GPU/Directx9/PixelShaderGeneratorDX9.cpp
	GPU/Directx9/PixelShaderGeneratorDX9.h
	GPU/Directx9/FramebufferDX9.cpp
	GPU/Directx9/FramebufferDX9.h
	GPU/Directx9/GPU_DX9.cpp
	GPU/Directx9/GPU_DX9.h
	GPU/Directx9/ShaderManagerDX9.cpp
	GPU/Directx9/ShaderManagerDX9.h
	GPU/Directx9/StateMappingDX9.cpp
	GPU/Directx9/StateMappingDX9.h
	GPU/Directx9/StencilBufferDX9.cpp
	GPU/Directx9/TextureCacheDX9.cpp
	GPU/Directx9/TextureCacheDX9.h
	GPU/Directx9/TextureScalerDX9.cpp
	GPU/Directx9/TextureScalerDX9.h
	GPU/Directx9/VertexShaderGeneratorDX9.cpp
	GPU/Directx9/VertexShaderGeneratorDX9.h
)

set(GPU_D3D11
  GPU/D3D11/DepalettizeShaderD3D11.cpp
	GPU/D3D11/DepalettizeShaderD3D11.h
	GPU/D3D11/DrawEngineD3D11.cpp
	GPU/D3D11/DrawEngineD3D11.h
	GPU/D3D11/FragmentShaderGeneratorD3D11.cpp
	GPU/D3D11/FragmentShaderGeneratorD3D11.h
	GPU/D3D11/FramebufferManagerD3D11.cpp
	GPU/D3D11/FramebufferManagerD3D11.h
	GPU/D3D11/GPU_D3D11.cpp
	GPU/D3D11/GPU_D3D11.h
	GPU/D3D11/D3D11Util.cpp
	GPU/D3D11/D3D11Util.h
	GPU/D3D11/ShaderManagerD3D11.cpp
	GPU/D3D11/ShaderManagerD3D11.h
	GPU/D3D11/StateMappingD3D11.cpp
	GPU/D3D11/StateMappingD3D11.h
	GPU/D3D11/StencilBufferD3D11.cpp
	GPU/D3D11/TextureCacheD3D11.cpp
	GPU/D3D11/TextureCacheD3D11.h
	GPU/D3D11/TextureScalerD3D11.cpp
	GPU/D3D11/TextureScalerD3D11.h
	GPU/D3D11/VertexShaderGeneratorD3D11.cpp
	GPU/D3D11/VertexShaderGeneratorD3D11.h
)

set(GPU_IMPLS ${GPU_GLES})
if(VULKAN)
  set(GPU_IMPLS ${GPU_IMPLS} ${GPU_VULKAN})
endif()
if(WIN32)
	list(APPEND GPU_IMPLS ${GPU_D3D9} ${GPU_D3D11})
endif()

if(ARMV7 OR ARM64)
	set(GPU_NEON GPU/Common/TextureDecoderNEON.cpp)
endif()
set(GPU_SOURCES
	${GPU_IMPLS}
	${GPU_NEON}
	GPU/Common/DepalettizeShaderCommon.cpp
	GPU/Common/DepalettizeShaderCommon.h
	GPU/Common/FramebufferCommon.cpp
	GPU/Common/FramebufferCommon.h
	GPU/Common/GPUDebugInterface.cpp
	GPU/Common/GPUDebugInterface.h
	GPU/Common/GPUStateUtils.cpp
	GPU/Common/GPUStateUtils.h
	GPU/Common/DrawEngineCommon.cpp
	GPU/Common/DrawEngineCommon.h
	GPU/Common/ShaderId.cpp
	GPU/Common/ShaderId.h
	GPU/Common/ShaderUniforms.cpp
	GPU/Common/ShaderUniforms.h
	GPU/Common/ShaderCommon.cpp
	GPU/Common/ShaderCommon.h
	GPU/Common/ShaderTranslation.cpp
	GPU/Common/ShaderTranslation.h
	GPU/Common/SplineCommon.cpp
	GPU/Common/SplineCommon.h
	GPU/Common/StencilCommon.cpp
	GPU/Common/StencilCommon.h
	GPU/Common/SoftwareTransformCommon.cpp
	GPU/Common/SoftwareTransformCommon.h
	GPU/Common/VertexDecoderCommon.cpp
	GPU/Common/VertexDecoderCommon.h
	GPU/Common/TransformCommon.cpp
	GPU/Common/TransformCommon.h
	GPU/Common/IndexGenerator.cpp
	GPU/Common/IndexGenerator.h
	GPU/Common/TextureDecoder.cpp
	GPU/Common/TextureDecoder.h
	GPU/Common/TextureCacheCommon.cpp
	GPU/Common/TextureCacheCommon.h
	GPU/Common/TextureScalerCommon.cpp
	GPU/Common/TextureScalerCommon.h
	GPU/Common/PostShader.cpp
	GPU/Common/PostShader.h
	GPU/Common/SplineCommon.h
	GPU/Debugger/Breakpoints.cpp
	GPU/Debugger/Breakpoints.h
	GPU/Debugger/Record.cpp
	GPU/Debugger/Record.h
	GPU/Debugger/Stepping.cpp
	GPU/Debugger/Stepping.h
	GPU/GPUInterface.h
	GPU/GeDisasm.cpp
	GPU/GeDisasm.h
	GPU/GPU.cpp
	GPU/GPU.h
	GPU/GPUCommon.cpp
	GPU/GPUCommon.h
	GPU/GPUState.cpp
	GPU/GPUState.h
	GPU/Math3D.cpp
	GPU/Math3D.h
	GPU/Null/NullGpu.cpp
	GPU/Null/NullGpu.h
	GPU/Software/Clipper.cpp
	GPU/Software/Clipper.h
	GPU/Software/Lighting.cpp
	GPU/Software/Lighting.h
	GPU/Software/Rasterizer.cpp
	GPU/Software/Rasterizer.h
	GPU/Software/Sampler.cpp
	GPU/Software/Sampler.h
	GPU/Software/SoftGpu.cpp
	GPU/Software/SoftGpu.h
	GPU/Software/TransformUnit.cpp
	GPU/Software/TransformUnit.h
	GPU/ge_constants.h)

# 'ppsspp_jni' on ANDROID, 'Core' everywhere else
# SHARED on ANDROID, STATIC everywhere else
add_library(${CoreLibName} ${CoreLinkType}
	${CoreExtra}
	Core/Config.cpp
	Core/Config.h
	Core/Core.cpp
	Core/Core.h
	Core/Compatibility.cpp
	Core/Compatibility.h
	Core/CoreParameter.h
	Core/CoreTiming.cpp
	Core/CoreTiming.h
	Core/CwCheat.cpp
	Core/CwCheat.h
	Core/HDRemaster.cpp
	Core/HDRemaster.h
	Core/ThreadEventQueue.h
	Core/Debugger/Breakpoints.cpp
	Core/Debugger/Breakpoints.h
	Core/Debugger/DebugInterface.h
	Core/Debugger/SymbolMap.cpp
	Core/Debugger/SymbolMap.h
	Core/Debugger/DisassemblyManager.cpp
	Core/Debugger/DisassemblyManager.h
	Core/Dialog/PSPDialog.cpp
	Core/Dialog/PSPDialog.h
	Core/Dialog/PSPGamedataInstallDialog.cpp
	Core/Dialog/PSPGamedataInstallDialog.h
	Core/Dialog/PSPMsgDialog.cpp
	Core/Dialog/PSPMsgDialog.h
	Core/Dialog/PSPNetconfDialog.cpp
	Core/Dialog/PSPNetconfDialog.h
	Core/Dialog/PSPOskDialog.cpp
	Core/Dialog/PSPOskDialog.h
	Core/Dialog/PSPPlaceholderDialog.cpp
	Core/Dialog/PSPPlaceholderDialog.h
	Core/Dialog/PSPSaveDialog.cpp
	Core/Dialog/PSPSaveDialog.h
	Core/Dialog/PSPScreenshotDialog.cpp
	Core/Dialog/PSPScreenshotDialog.h
	Core/Dialog/SavedataParam.cpp
	Core/Dialog/SavedataParam.h
	Core/ELF/ElfReader.cpp
	Core/ELF/ElfReader.h
	Core/ELF/ElfTypes.h
	Core/ELF/PBPReader.cpp
	Core/ELF/PBPReader.h
	Core/ELF/PrxDecrypter.cpp
	Core/ELF/PrxDecrypter.h
	Core/ELF/ParamSFO.cpp
	Core/ELF/ParamSFO.h
	Core/FileSystems/tlzrc.cpp
	Core/FileSystems/BlobFileSystem.cpp
	Core/FileSystems/BlobFileSystem.h
	Core/FileSystems/BlockDevices.cpp
	Core/FileSystems/BlockDevices.h
	Core/FileSystems/DirectoryFileSystem.cpp
	Core/FileSystems/DirectoryFileSystem.h
	Core/FileSystems/FileSystem.h
	Core/FileSystems/FileSystem.cpp
	Core/FileSystems/ISOFileSystem.cpp
	Core/FileSystems/ISOFileSystem.h
	Core/FileSystems/MetaFileSystem.cpp
	Core/FileSystems/MetaFileSystem.h
	Core/FileSystems/VirtualDiscFileSystem.cpp
	Core/FileSystems/VirtualDiscFileSystem.h
	Core/Font/PGF.cpp
	Core/Font/PGF.h
	Core/HLE/FunctionWrappers.h
	Core/HLE/HLE.cpp
	Core/HLE/HLE.h
	Core/HLE/ReplaceTables.cpp
	Core/HLE/ReplaceTables.h
	Core/HLE/HLEHelperThread.cpp
	Core/HLE/HLEHelperThread.h
	Core/HLE/HLETables.cpp
	Core/HLE/HLETables.h
	Core/HLE/KernelWaitHelpers.h
	Core/HLE/KUBridge.h
	Core/HLE/KUBridge.cpp
	Core/HLE/ThreadQueueList.h
	Core/HLE/__sceAudio.cpp
	Core/HLE/__sceAudio.h
	Core/HLE/sceAdler.cpp
	Core/HLE/sceAdler.h
	Core/HLE/sceAtrac.cpp
	Core/HLE/sceAtrac.h
	Core/HLE/sceAudio.cpp
	Core/HLE/sceAudiocodec.cpp
	Core/HLE/sceAudiocodec.h
	Core/HLE/sceAudioRouting.cpp
	Core/HLE/sceAudioRouting.h
	Core/HLE/sceAudio.h
	Core/HLE/sceCcc.h
	Core/HLE/sceCcc.cpp
	Core/HLE/sceChnnlsv.cpp
	Core/HLE/sceChnnlsv.h
	Core/HLE/sceCtrl.cpp
	Core/HLE/sceCtrl.h
	Core/HLE/sceDeflt.cpp
	Core/HLE/sceDeflt.h
	Core/HLE/sceDisplay.cpp
	Core/HLE/sceDisplay.h
	Core/HLE/sceDmac.cpp
	Core/HLE/sceDmac.h
	Core/HLE/sceG729.cpp
	Core/HLE/sceG729.h
	Core/HLE/sceGameUpdate.cpp
	Core/HLE/sceGameUpdate.h
	Core/HLE/sceGe.cpp
	Core/HLE/sceGe.h
	Core/HLE/sceFont.cpp
	Core/HLE/sceFont.h
	Core/HLE/sceHeap.cpp
	Core/HLE/sceHeap.h
	Core/HLE/sceHprm.cpp
	Core/HLE/sceHprm.h
	Core/HLE/sceHttp.cpp
	Core/HLE/sceHttp.h
	Core/HLE/sceImpose.cpp
	Core/HLE/sceImpose.h
	Core/HLE/sceIo.cpp
	Core/HLE/sceIo.h
	Core/HLE/sceJpeg.cpp
	Core/HLE/sceJpeg.h
	Core/HLE/sceKernel.cpp
	Core/HLE/sceKernel.h
	Core/HLE/sceKernelAlarm.cpp
	Core/HLE/sceKernelAlarm.h
	Core/HLE/sceKernelEventFlag.cpp
	Core/HLE/sceKernelEventFlag.h
	Core/HLE/sceKernelInterrupt.cpp
	Core/HLE/sceKernelInterrupt.h
	Core/HLE/sceKernelMbx.cpp
	Core/HLE/sceKernelMbx.h
	Core/HLE/sceKernelMemory.cpp
	Core/HLE/sceKernelMemory.h
	Core/HLE/sceKernelModule.cpp
	Core/HLE/sceKernelModule.h
	Core/HLE/sceKernelMsgPipe.cpp
	Core/HLE/sceKernelMsgPipe.h
	Core/HLE/sceKernelMutex.cpp
	Core/HLE/sceKernelMutex.h
	Core/HLE/sceKernelSemaphore.cpp
	Core/HLE/sceKernelSemaphore.h
	Core/HLE/sceKernelThread.cpp
	Core/HLE/sceKernelThread.h
	Core/HLE/sceKernelTime.cpp
	Core/HLE/sceKernelTime.h
	Core/HLE/sceKernelVTimer.cpp
	Core/HLE/sceKernelVTimer.h
	Core/HLE/sceMpeg.cpp
	Core/HLE/sceMpeg.h
	Core/HLE/sceNet.cpp
	Core/HLE/sceNet.h
	Core/HLE/sceNetAdhoc.cpp
	Core/HLE/sceNetAdhoc.h
	Core/HLE/proAdhoc.h
	Core/HLE/proAdhoc.cpp
	Core/HLE/proAdhocServer.h
	Core/HLE/proAdhocServer.cpp
	Core/HLE/sceOpenPSID.cpp
	Core/HLE/sceOpenPSID.h
	Core/HLE/sceP3da.cpp
	Core/HLE/sceP3da.h
	Core/HLE/sceMt19937.cpp
	Core/HLE/sceMt19937.h
	Core/HLE/sceMd5.cpp
	Core/HLE/sceMd5.h
	Core/HLE/sceMp4.cpp
	Core/HLE/sceMp4.h
	Core/HLE/sceMp3.cpp
	Core/HLE/sceMp3.h
	Core/HLE/sceParseHttp.cpp
	Core/HLE/sceParseHttp.h
	Core/HLE/sceParseUri.cpp
	Core/HLE/sceParseUri.h
	Core/HLE/scePower.cpp
	Core/HLE/scePower.h
	Core/HLE/scePsmf.cpp
	Core/HLE/scePsmf.h
	Core/HLE/sceRtc.cpp
	Core/HLE/sceRtc.h
	Core/HLE/sceSas.cpp
	Core/HLE/sceSas.h
	Core/HLE/sceSfmt19937.cpp
	Core/HLE/sceSfmt19937.h
	Core/HLE/sceSha256.cpp
	Core/HLE/sceSha256.h
	Core/HLE/sceSsl.cpp
	Core/HLE/sceSsl.h
	Core/HLE/sceUmd.cpp
	Core/HLE/sceUmd.h
	Core/HLE/sceUsb.cpp
	Core/HLE/sceUsb.h
	Core/HLE/sceUsbCam.cpp
	Core/HLE/sceUsbCam.h
	Core/HLE/sceUsbGps.cpp
	Core/HLE/sceUsbGps.h
	Core/HLE/sceUtility.cpp
	Core/HLE/sceUtility.h
	Core/HLE/sceVaudio.cpp
	Core/HLE/sceVaudio.h
	Core/HLE/scePspNpDrm_user.cpp
	Core/HLE/scePspNpDrm_user.h
	Core/HLE/sceNp.cpp
	Core/HLE/sceNp.h
	Core/HLE/scePauth.cpp
	Core/HLE/scePauth.h
	Core/HW/SimpleAudioDec.cpp
	Core/HW/SimpleAudioDec.h
	Core/HW/AsyncIOManager.cpp
	Core/HW/AsyncIOManager.h
	Core/HW/MediaEngine.cpp
	Core/HW/MediaEngine.h
	Core/HW/MpegDemux.cpp
	Core/HW/MpegDemux.h
	Core/HW/MemoryStick.cpp
	Core/HW/MemoryStick.h
	Core/HW/SasAudio.cpp
	Core/HW/SasAudio.h
	Core/HW/SasReverb.cpp
	Core/HW/SasReverb.h
	Core/HW/StereoResampler.cpp
	Core/HW/StereoResampler.h
	Core/Host.cpp
	Core/Host.h
	Core/Loaders.cpp
	Core/Loaders.h
	Core/FileLoaders/CachingFileLoader.cpp
	Core/FileLoaders/CachingFileLoader.h
	Core/FileLoaders/DiskCachingFileLoader.cpp
	Core/FileLoaders/DiskCachingFileLoader.h
	Core/FileLoaders/HTTPFileLoader.cpp
	Core/FileLoaders/HTTPFileLoader.h
	Core/FileLoaders/LocalFileLoader.cpp
	Core/FileLoaders/LocalFileLoader.h
	Core/FileLoaders/RamCachingFileLoader.cpp
	Core/FileLoaders/RamCachingFileLoader.h
	Core/FileLoaders/RetryingFileLoader.cpp
	Core/FileLoaders/RetryingFileLoader.h
	Core/MIPS/JitCommon/JitCommon.cpp
	Core/MIPS/JitCommon/JitCommon.h
	Core/MIPS/JitCommon/JitBlockCache.cpp
	Core/MIPS/JitCommon/JitBlockCache.h
	Core/MIPS/JitCommon/JitState.cpp
	Core/MIPS/JitCommon/JitState.h
	Core/MIPS/MIPS.cpp
	Core/MIPS/MIPS.h
	Core/MIPS/MIPSAnalyst.cpp
	Core/MIPS/MIPSAnalyst.h
	Core/MIPS/MIPSCodeUtils.cpp
	Core/MIPS/MIPSCodeUtils.h
	Core/MIPS/MIPSDebugInterface.cpp
	Core/MIPS/MIPSDebugInterface.h
	Core/MIPS/MIPSDis.cpp
	Core/MIPS/MIPSDis.h
	Core/MIPS/MIPSDisVFPU.cpp
	Core/MIPS/MIPSDisVFPU.h
	Core/MIPS/MIPSInt.cpp
	Core/MIPS/MIPSInt.h
	Core/MIPS/MIPSIntVFPU.cpp
	Core/MIPS/MIPSIntVFPU.h
	Core/MIPS/MIPSStackWalk.cpp
	Core/MIPS/MIPSStackWalk.h
	Core/MIPS/MIPSTables.cpp
	Core/MIPS/MIPSTables.h
	Core/MIPS/MIPSVFPUUtils.cpp
	Core/MIPS/MIPSVFPUUtils.h
	Core/MIPS/MIPSAsm.cpp
	Core/MIPS/MIPSAsm.h
	Core/MemMap.cpp
	Core/MemMap.h
	Core/MemMapFunctions.cpp
	Core/MemMapHelpers.h
	Core/PSPLoaders.cpp
	Core/PSPLoaders.h
	Core/Reporting.cpp
	Core/Reporting.h
	Core/SaveState.cpp
	Core/SaveState.h
	Core/Screenshot.cpp
	Core/Screenshot.h
	Core/System.cpp
	Core/System.h
	Core/TextureReplacer.cpp
	Core/TextureReplacer.h
	Core/Util/AudioFormat.cpp
	Core/Util/AudioFormat.h
	Core/Util/GameManager.cpp
	Core/Util/GameManager.h
	Core/Util/BlockAllocator.cpp
	Core/Util/BlockAllocator.h
	Core/Util/PPGeDraw.cpp
	Core/Util/PPGeDraw.h
	Core/Util/ppge_atlas.cpp
	Core/Util/ppge_atlas.h
	${CORE_NEON}
	${GPU_SOURCES}
	git-version.cpp
	ext/disarm.cpp
	git-version.cpp)

if(ANDROID)
  set(CoreExtraLibs ${CoreExtraLibs} android)
  if(X86_64)
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,-Bsymbolic")
  endif()
endif()

set(CoreExtraLibs ${CoreExtraLibs} armips)

# needed for VK_USE_PLATFORM_XCB_KHR only
#if(VULKAN AND NOT WIN32)
#	target_link_libraries(native X11-xcb X11)
#endif()


set(GlslangLibs glslang OGLCompiler OSDependent SPIRV SPVRemapper spirv-cross-glsl)

target_link_libraries(${CoreLibName} Common native kirk cityhash sfmt19937 xbrz xxhash ${GlslangLibs}
	${CoreExtraLibs} ${OPENGL_LIBRARIES} ${X11_LIBRARIES} ${CMAKE_DL_LIBS})

if(FFmpeg_FOUND)
  target_compile_definitions(${CoreLibName} PRIVATE USE_FFMPEG=1)
  target_link_libraries(${CoreLibName}
    FFmpeg::avcodec
    FFmpeg::avformat
    FFmpeg::avutil
    FFmpeg::swresample
    FFmpeg::swscale
  )
endif()

setup_target_project(${CoreLibName} Core)

# Generate git-version.cpp at build time.
add_custom_target(GitVersion ALL
	DEPENDS something_that_never_exists)
add_custom_command(OUTPUT something_that_never_exists
	COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
		-P ${CMAKE_CURRENT_SOURCE_DIR}/git-version.cmake)

set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/git-version.cpp
	PROPERTIES GENERATED TRUE)
add_dependencies(${CoreLibName} GitVersion)

set(WindowsFiles
	Windows/DSoundStream.cpp
	Windows/DSoundStream.h
	Windows/Debugger/CPURegsInterface.h
	Windows/Debugger/BreakpointWindow.cpp
	Windows/Debugger/BreakpointWindow.h
	Windows/Debugger/DumpMemoryWindow.cpp
	Windows/Debugger/DumpMemoryWindow.h
	Windows/Debugger/CtrlDisAsmView.cpp
	Windows/Debugger/CtrlDisAsmView.h
	Windows/Debugger/CtrlMemView.cpp
	Windows/Debugger/CtrlMemView.h
	Windows/Debugger/CtrlRegisterList.cpp
	Windows/Debugger/CtrlRegisterList.h
	Windows/Debugger/Debugger.h
	Windows/Debugger/DebuggerShared.cpp
	Windows/Debugger/DebuggerShared.h
	Windows/Debugger/Debugger_Disasm.cpp
	Windows/Debugger/Debugger_Disasm.h
	Windows/Debugger/Debugger_MemoryDlg.cpp
	Windows/Debugger/Debugger_MemoryDlg.h
	Windows/Debugger/Debugger_Lists.cpp
	Windows/Debugger/Debugger_Lists.h
	Windows/Debugger/Debugger_Misc.cpp
	Windows/Debugger/Debugger_Misc.h
	Windows/Debugger/Debugger_SymbolMap.h
	Windows/Debugger/Debugger_VFPUDlg.cpp
	Windows/Debugger/Debugger_VFPUDlg.h
	Windows/Debugger/SimpleELF.h
	Windows/GEDebugger/CtrlDisplayListView.cpp
	Windows/GEDebugger/SimpleGLWindow.cpp
	Windows/GEDebugger/TabState.cpp
	Windows/GEDebugger/VertexPreview.cpp
	Windows/GEDebugger/CtrlDisplayListView.h
	Windows/GEDebugger/SimpleGLWindow.h
	Windows/GEDebugger/TabState.h
	Windows/GEDebugger/GEDebugger.cpp
	Windows/GEDebugger/TabDisplayLists.cpp
	Windows/GEDebugger/TabVertices.cpp
	Windows/GEDebugger/GEDebugger.h
	Windows/GEDebugger/TabDisplayLists.h
	Windows/GEDebugger/TabVertices.h
	Windows/DinputDevice.cpp
	Windows/DinputDevice.h
	Windows/DSoundStream.cpp
	Windows/DSoundStream.h
	Windows/EmuThread.cpp
	Windows/EmuThread.h
	Windows/GeDebugger/GeDebugger.cpp
	Windows/Globals.cpp
	Windows/GPU/D3D9Context.cpp
	Windows/GPU/D3D9Context.h
	Windows/GPU/D3D11Context.cpp
	Windows/GPU/D3D11Context.h
	Windows/GPU/WindowsGLContext.cpp
	Windows/GPU/WindowsVulkanContext.cpp
	Windows/InputBox.cpp
	Windows/InputBox.h
	Windows/InputDevice.cpp
	Windows/InputDevice.h
	Windows/KeyboardDevice.cpp
	Windows/KeyboardDevice.h
	Windows/W32Util/DialogManager.cpp
	Windows/W32Util/DialogManager.h
	Windows/W32Util/Misc.cpp
	Windows/W32Util/Misc.h
	Windows/W32Util/PropertySheet.cpp
	Windows/W32Util/PropertySheet.h
	Windows/W32Util/ShellUtil.cpp
	Windows/W32Util/ShellUtil.h
	Windows/W32Util/TabControl.cpp
	Windows/W32Util/TabControl.h
	Windows/W32Util/PropertySheet.cpp
	Windows/W32Util/PropertySheet.h
	Windows/WindowsHost.cpp
	Windows/WindowsHost.h
	Windows/MainWindow.cpp
	Windows/MainWindow.h
	Windows/MainWindowMenu.cpp
	Windows/MainWindowMenu.h
	Windows/RawInput.cpp
	Windows/RawInput.h
	Windows/TouchInputHandler.cpp
	Windows/TouchInputHandler.h
	Windows/XPTheme.h
	Windows/XinputDevice.cpp
	Windows/XinputDevice.h
	Windows/main.cpp
	Windows/main.h
	Windows/ppsspp.rc
	Windows/resource.h
	Windows/stdafx.cpp
	Windows/stdafx.h)

list(APPEND LinkCommon ${CoreLibName} ${CMAKE_THREAD_LIBS_INIT})

if(WIN32)
	list(APPEND LinkCommon kernel32 user32 gdi32 shell32 comctl32 dsound xinput)
	#setup_target_project(${TargetBin} Windows)
	list(APPEND NativeAppSource ${WindowsFiles})
endif()

set(NativeAssets
	android/assets/ui_atlas.zim
	assets/lang
	assets/shaders
	assets/Roboto-Condensed.ttf
	assets/7z.png
	assets/compat.ini
	assets/gamecontrollerdb.txt
	assets/langregion.ini
	assets/ppge_atlas.zim
	assets/rargray.png
	assets/unknown.png
	assets/zip.png)


if(HEADLESS)
	add_executable(PPSSPPHeadless
		headless/Headless.cpp
		headless/StubHost.cpp
		headless/StubHost.h
		headless/Compare.cpp
		headless/Compare.h
		headless/SDLHeadlessHost.cpp
		headless/SDLHeadlessHost.h)
	target_link_libraries(PPSSPPHeadless
		${COCOA_LIBRARY} ${LinkCommon})
	setup_target_project(PPSSPPHeadless headless)
endif()

if(UNITTEST)
	add_executable(unitTest
		unittest/UnitTest.cpp
		unittest/TestArmEmitter.cpp
		unittest/TestArm64Emitter.cpp
		unittest/TestX64Emitter.cpp
		unittest/TestVertexJit.cpp
		unittest/JitHarness.cpp
		Core/MIPS/ARM/ArmRegCache.cpp
		Core/MIPS/ARM/ArmRegCacheFPU.cpp
	)
	target_link_libraries(unitTest
		${COCOA_LIBRARY} ${LinkCommon} Common)
	setup_target_project(unitTest unittest)
endif()

if (TargetBin)
	if (IOS OR APPLE)
		if (APPLE AND NOT IOS)
		set(ICON_PATH_ABS ${CMAKE_CURRENT_SOURCE_DIR}/icons/ppsspp.icns)
		set( MACOSX_BUNDLE_ICON_FILE ppsspp.icns )
		set_source_files_properties(${ICON_PATH_ABS} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
		endif()

		# TODO: there must a native way to copy these.
		# Now this is very prone to errors when changes occur.
		# Also better to have assets under Resources dir for OS X.
		file(GLOB_RECURSE FLASH0_FILES assets/flash0/*)
		file(GLOB_RECURSE LANG_FILES assets/lang/*)
		file(GLOB_RECURSE SHADER_FILES assets/shaders/*)

		if (APPLE AND NOT IOS)
		set_source_files_properties(${NativeAssets} PROPERTIES MACOSX_PACKAGE_LOCATION "MacOS/assets")
		set_source_files_properties(${FLASH0_FILES} PROPERTIES MACOSX_PACKAGE_LOCATION "MacOS/assets/flash0/font")
		set_source_files_properties(${LANG_FILES} PROPERTIES MACOSX_PACKAGE_LOCATION "MacOS/assets/lang")
		set_source_files_properties(${SHADER_FILES} PROPERTIES MACOSX_PACKAGE_LOCATION "MacOS/assets/shaders")
		endif()

		if (IOS)
		add_executable(${TargetBin} MACOSX_BUNDLE ${ICON_PATH_ABS} ${NativeAssets} ${SHADER_FILES} ${FLASH0_FILES} ${LANG_FILES} ${NativeAppSource} "ios/Launch Screen.storyboard")
		else()
		add_executable(${TargetBin} MACOSX_BUNDLE ${ICON_PATH_ABS} ${NativeAssets} ${SHADER_FILES} ${FLASH0_FILES} ${LANG_FILES} ${NativeAppSource})
		endif()
	elseif(WIN32)
		add_executable(${TargetBin} WIN32 ${NativeAppSource})
		set_target_properties(${TargetBin} PROPERTIES LINK_FLAGS "/SUBSYSTEM:WINDOWS")
		set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${TargetBin})
	else()
		add_executable(${TargetBin} ${NativeAppSource})
	endif()
	target_link_libraries(${TargetBin} ${LinkCommon} Common)
endif()

# installs
if(NOT ANDROID)
file(INSTALL ${NativeAssets} DESTINATION assets)
file(INSTALL assets/flash0 DESTINATION assets)
endif()
# packaging and code signing
if(IOS)
	set(DEPLOYMENT_TARGET 8.0)
	file(GLOB IOSAssets ios/assets/*.png)
	list(REMOVE_ITEM IOSAssets ${CMAKE_CURRENT_SOURCE_DIR}/ios/assets/Default-568h@2x.png)
	list(REMOVE_ITEM IOSAssets ${CMAKE_CURRENT_SOURCE_DIR}/ios/assets/Default-568h@3x.png)
	file(INSTALL ${IOSAssets} DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/assets)
	file(GLOB IOSAssets ios/assets/Default-568h@*.png)
	file(INSTALL ${IOSAssets} DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
	if (IOS_DEBUG)
		file(INSTALL pspautotests DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/assets)
	endif()
	set(RSRC_XIB_FILES assets/Icon@2x.png)
	set(RSRC_XIB_FILES "Launch Screen.storyboard")
	set_source_files_properties(${RSRC_XIB_FILES}
		PROPERTIES MACOSX_PACKAGE_LOCATION Resources
	)
    if(CMAKE_GENERATOR STREQUAL "Xcode")
	  set(APP_DIR_NAME "$(TARGET_BUILD_DIR)/$(FULL_PRODUCT_NAME)")
    else()
      set(APP_DIR_NAME "$<TARGET_FILE_DIR:PPSSPP>")
    endif()
	add_custom_command(TARGET PPSSPP POST_BUILD
		COMMAND mkdir -p \"${APP_DIR_NAME}\"
		COMMAND tar -c -C ${CMAKE_CURRENT_BINARY_DIR} --exclude .DS_Store --exclude .git assets *.png | tar -x -C \"${APP_DIR_NAME}\"
	)
	set(MACOSX_DEPLOYMENT_TARGET ${DEPLOYMENT_TARGET})
	set_target_properties(${TargetBin} PROPERTIES
		MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/ios/PPSSPP-Info.plist"
		RESOURCE "ios/Launch Screen.storyboard"
		XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET ${DEPLOYMENT_TARGET}
		XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "iPhone/iPad"
        XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC YES
        XCODE_ATTRIBUTE_ENABLE_BITCODE NO
        XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "-"
	)
endif()
