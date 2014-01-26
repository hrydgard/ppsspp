# vim:noexpandtab:
cmake_minimum_required(VERSION 2.8.8)
project(PPSSPP)

add_definitions(-DPPSSPP)

if(ANDROID)
	if(ARMEABI OR ARMEABI_V7A)
		set(ARM ON)
		set(USE_FFMPEG ON)
	endif()
endif()

if(BLACKBERRY)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D__STDC_CONSTANT_MACROS")
endif()

if (IOS OR BLACKBERRY)
	if (SIMULATOR)
		set(X86 ON)
	else()
		set(ARM ON)
		set(ARMEABI_V7A ON)
		set(USE_FFMPEG ON)
	endif()
endif()

if (MAEMO)
	set(ARM ON)
	#Maemo's gcc-4.7.2 is strict
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fpermissive")
endif()

if(LOONGSON)
	set(MIPS ON) # MIPS + x86 for now
endif()

if(ARM OR SIMULATOR)
	set(USING_GLES2 ON)
else() # Assume x86
	set(X86 ON)
endif()

if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
	set(LINUX ON)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D__STDC_CONSTANT_MACROS")
	if (NOT USING_GLES2)
		set(USE_FFMPEG ON)
	endif()
endif()

if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
	set(MACOSX ON)
	set(USE_FFMPEG ON)
endif()

if(NOT DEFINED HEADLESS)
	set(HEADLESS OFF)
endif()

# Doesn't link on some platforms
#if(NOT DEFINED UNITTEST)
#	set(UNITTEST OFF)
#endif()

# User-editable options (go into CMakeCache.txt)
option(ARM "Set to ON if targeting an ARM processor" ${ARM})
option(MIPS "Set to ON if targeting a MIPS processor" ${MIPS})
option(X86 "Set to ON if targeting an X86 processor" ${X86})
option(ANDROID "Set to ON if targeting an Android device" ${ANDROID})
option(BLACKBERRY "Set to ON if targeting a Blackberry device" ${BLACKBERRY})
option(PANDORA "Set to ON if targeting an OpenPandora device" ${PANDORA})
option(MAEMO "Set to ON if targeting an Maemo (N900) device" ${MAEMO})
option(IOS "Set to ON if targeting an iOS device" ${IOS})
option(USING_GLES2 "Set to ON if target device uses OpenGL ES 2.0" ${USING_GLES2})
option(USING_QT_UI "Set to ON if you wish to use the Qt frontend wrapper" ${USING_QT_UI})
option(HEADLESS "Set to OFF to not generate the PPSSPPHeadless target" ${HEADLESS})
option(UNITTEST "Set to ON to generate the unittest target" ${UNITTEST})
option(SIMULATOR "Set to ON when targeting an x86 simulator of an ARM platform" ${SIMULATOR})
option(USE_FFMPEG "Build with FFMPEG support" ${USE_FFMPEG})

if(ANDROID OR BLACKBERRY OR IOS)
	if (NOT CMAKE_TOOLCHAIN_FILE)
		if (ANDROID)
			set(CMAKE_TOOLCHAIN_FILE ${CMAKE_SOURCE_DIR}/android/android.toolchain.cmake)
		elseif(BLACKBERRY)
			set(CMAKE_TOOLCHAIN_FILE ${CMAKE_SOURCE_DIR}/Blackberry/bb.toolchain.cmake)
		elseif(IOS)
			set(CMAKE_TOOLCHAIN_FILE ${CMAKE_SOURCE_DIR}/ios/ios.toolchain.cmake)
		endif()
		message(FATAL_ERROR "CMAKE_TOOLCHAIN_FILE was not set!\n"
			"Delete the CMakeCache.txt file and CMakeFiles directory.\n"
			"Re-run ${CMAKE_COMMAND} with:\n"
			"\"-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}\"")
	endif()
endif()

if(ANDROID)
	set(CoreLibName ppsspp_jni)
	set(CoreLinkType SHARED)
else()
	set(CoreLibName Core)
	set(CoreLinkType STATIC)
endif()

#find_package(Qt5Widgets)
if(USING_GLES2 AND NOT IOS)
	set(OPENGL_LIBRARIES GLESv2)
elseif(NOT IOS)
	include(FindOpenGL REQUIRED)
endif()
if (NOT BLACKBERRY AND NOT ANDROID AND NOT IOS)
	include(FindSDL)
endif()
include(FindThreads)
if(APPLE)
	find_library(COCOA_LIBRARY Cocoa)
endif()

# Needed for Globals.h
include_directories("${CMAKE_SOURCE_DIR}")

if(BLACKBERRY)
	add_definitions(-DBLACKBERRY=${BLACKBERRY})
endif()

if(ANDROID)
	add_definitions(-DANDROID)
endif()

if(IOS)
	add_definitions(-DIOS)
endif()

if(MAEMO)
	add_definitions(-DMAEMO)
endif()

if(PANDORA)
	add_definitions(-DPANDORA)
endif()

if(ARM)
	add_definitions(-DARM)
endif()
if(USING_GLES2)
	add_definitions(-DUSING_GLES2)
endif()

if(MIPS)
	add_definitions(-DMIPS)
endif()

set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -D_DEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -D_NDEBUG")

if(NOT MSVC)
	# Disable some warnings
	set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O2")
	add_definitions(-Wno-multichar)
	add_definitions(-fno-strict-aliasing)
	if (CMAKE_C_COMPILER_ID STREQUAL "Intel")
		set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -parallel -fopenmp")
	endif()
	if(NOT APPLE)
		if (NOT CMAKE_C_COMPILER_ID STREQUAL "Intel" AND NOT CMAKE_C_COMPILER_ID STREQUAL "Clang")
			add_definitions(-Wno-psabi)
		endif()
		add_definitions(-D_XOPEN_SOURCE=600 -D_XOPEN_SOURCE_EXTENDED -D__BSD_VISIBLE=1)
		add_definitions(-D_LARGEFILE64_SOURCE=1 -D_FILE_OFFSET_BITS=64)
	endif()
	if(IOS)
		add_definitions(-DGL_ETC1_RGB8_OES=0)
	endif()

	if(BLACKBERRY)
		add_definitions(-D_QNX_SOURCE)
		if(ARM)
			add_definitions(-DARMV7 -O3 -mfpu=neon -mcpu=cortex-a9)
		endif()
	endif()

	if(X86 AND NOT MIPS)
		# enable sse2 code generation
		add_definitions(-msse2)
	endif()

	if(IOS)
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libstdc++")
		# armv7s (without resorting to FastMemory) is still a work in progress
		# comment out the next line to enable default/"standard" architectures (which is a fat armv7/armv7s binary)
		set(CMAKE_OSX_ARCHITECTURES "armv7")
		add_definitions(-DARMV7)
	elseif(APPLE)
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++ -U__STRICT_ANSI__")
		# Karen/angelXwind: --macosx-version-min=10.7 is needed in order to produce binaries that OS X 10.7 Lion can execute. However, it seems that PPSSPP won't support 10.6 or lower without getting rid of -stdlib=libc++ ...which probably won't end well. So I guess PPSSPP will strictly be a 10.7+ app.
		# vit9696: OSX 10.6 builds are possible: http://forums.ppsspp.org/showthread.php?tid=1826&pid=18875#pid18875
		set(CMAKE_XCODE_ATTRIBUTE_CLANG_CXX_LIBRARY "libc++")
	endif()
	if(CMAKE_COMPILER_IS_GNUCXX AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 4.7.0)
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++0x")
	else()
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
	endif()
else()
	# Disable warnings about MS-specific _s variants of libc functions
	add_definitions(-D_CRT_SECURE_NO_WARNINGS)
endif()

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

if(ANDROID)
	set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/android/libs/${ANDROID_ABI}")
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

set(CommonExtra)
if(ARM)
	set(CommonExtra ${CommonExtra}
		Common/ArmCPUDetect.cpp
		Common/ArmEmitter.h
		Common/ArmEmitter.cpp
		Common/ArmThunk.cpp)
elseif(X86)
	set(CommonExtra ${CommonExtra}
		Common/ABI.cpp
		Common/ABI.h
		Common/ArmEmitter.h
		Common/ArmEmitter.cpp
		Common/CPUDetect.cpp
		Common/CPUDetect.h
		Common/Thunk.cpp
		Common/Thunk.h
		Common/x64Analyzer.cpp
		Common/x64Analyzer.h
		Common/x64Emitter.cpp
		Common/x64Emitter.h)
endif()
if(WIN32)
	set(CommonExtra ${CommonExtra}
		Common/stdafx.cpp
		Common/stdafx.h)
endif()

add_library(Common STATIC
	${CommonExtra}
	Common/ChunkFile.cpp
	Common/ChunkFile.h
	Common/ConsoleListener.cpp
	Common/ConsoleListener.h
	Common/Crypto/md5.cpp
	Common/Crypto/md5.h
	Common/Crypto/sha1.cpp
	Common/Crypto/sha1.h
	Common/FileUtil.cpp
	Common/FileUtil.h
	Common/KeyMap.cpp
	Common/KeyMap.h
	Common/LogManager.cpp
	Common/LogManager.h
	Common/MemArena.cpp
	Common/MemArena.h
	Common/MemoryUtil.cpp
	Common/MemoryUtil.h
	Common/Misc.cpp
	Common/MsgHandler.cpp
	Common/MsgHandler.h
	Common/StringUtils.cpp
	Common/StringUtils.h
	Common/ThreadPools.cpp
	Common/ThreadPools.h
	Common/Timer.cpp
	Common/Timer.h)
include_directories(Common)
setup_target_project(Common Common)

if(WIN32)
	target_link_libraries(Common winmm)
endif()

if(NOT USING_GLES2)
	include_directories(${OPENGL_INCLUDE_DIR})

	add_definitions(-DGLEW_STATIC)
	add_library(glew STATIC
		native/ext/glew/GL/glew.h
		native/ext/glew/GL/glxew.h
		native/ext/glew/GL/wglew.h
		native/ext/glew/glew.c)
	target_link_libraries(glew ${OPENGL_LIBRARIES})
	include_directories(native/ext/glew)
	set(GLEW_LIBRARIES glew)
endif()

add_library(snappy STATIC
	ext/snappy/snappy-c.cpp
	ext/snappy/snappy-internal.h
	ext/snappy/snappy-sinksource.h
	ext/snappy/snappy-stubs-internal.h
	ext/snappy/snappy-stubs-public.h
	ext/snappy/snappy.cpp
	ext/snappy/snappy.h
)
include_directories(ext/snappy)

add_library(vjson STATIC
	native/ext/vjson/json.cpp
	native/ext/vjson/json.h
	native/ext/vjson/block_allocator.cpp
	native/ext/vjson/block_allocator.h
)

add_library(rg_etc1 STATIC
	native/ext/rg_etc1/rg_etc1.cpp
	native/ext/rg_etc1/rg_etc1.h)
include_directories(native/ext/rg_etc1)

add_library(stb_vorbis STATIC
	native/ext/stb_vorbis/stb_vorbis.c
	native/ext/stb_vorbis/stb_vorbis.h)
include_directories(native/ext/stb_vorbis)

if(USE_FFMPEG AND NOT DEFINED FFMPEG_BUILDDIR)
	if(BLACKBERRY)
		set(PLATFORM_ARCH "blackberry/armv7")
	elseif(IOS)
		set(PLATFORM_ARCH "ios/universal")
	elseif(MACOSX)
		set(PLATFORM_ARCH "macosx/x86_64")
	elseif(LINUX)
		if(CMAKE_SIZEOF_VOID_P EQUAL 8)
			set(PLATFORM_ARCH "linux/x86_64")
		else()
			set(PLATFORM_ARCH "linux/x86")
		endif()
	endif()
	# Using static libraries
	if (DEFINED PLATFORM_ARCH)
		include_directories(ffmpeg/${PLATFORM_ARCH}/include)
		link_directories(ffmpeg/${PLATFORM_ARCH}/lib)
		set(FFMPEG_LIBRARIES libavformat.a libavcodec.a libavutil.a libswresample.a libswscale.a)
	else()
		# Manual definition of system library locations by the user.
		if (DEFINED FFMPEG_INCLUDE_PATH)
			include_directories(ffmpeg ${FFMPEG_INCLUDE_PATH})
		endif()
		if (DEFINED AVFORMAT_PATH)
			add_library(libavformat STATIC IMPORTED)
			set_target_properties(libavformat PROPERTIES IMPORTED_LOCATION ${AVFORMAT_PATH})
			SET (FFMPEG_LIBRARIES ${FFMPEG_LIBRARIES} libavformat)
		endif()
		if (DEFINED AVCODEC_PATH)
			add_library(libavcodec STATIC IMPORTED)
			set_target_properties(libavcodec PROPERTIES IMPORTED_LOCATION ${AVCODEC_PATH})
			SET (FFMPEG_LIBRARIES ${FFMPEG_LIBRARIES} libavcodec)
		endif()
		if (DEFINED AVUTIL_PATH)
			add_library(libavutil STATIC IMPORTED)
			set_target_properties(libavutil PROPERTIES IMPORTED_LOCATION ${AVUTIL_PATH})
			SET (FFMPEG_LIBRARIES ${FFMPEG_LIBRARIES} libavutil)
		endif()
		if (DEFINED SWRESAMPLE_PATH)
			add_library(libswresample STATIC IMPORTED)
			set_target_properties(libswresample PROPERTIES IMPORTED_LOCATION ${SWRESAMPLE_PATH})
			SET (FFMPEG_LIBRARIES ${FFMPEG_LIBRARIES} libswresample)
		endif()
		if (DEFINED SWSCALE_PATH)
			add_library(libswscale STATIC IMPORTED)
			set_target_properties(libswscale PROPERTIES IMPORTED_LOCATION ${SWSCALE_PATH})
			SET (FFMPEG_LIBRARIES ${FFMPEG_LIBRARIES} libswscale)
		endif()
	endif(DEFINED PLATFORM_ARCH)
endif(USE_FFMPEG AND NOT DEFINED FFMPEG_BUILDDIR)

if(USE_FFMPEG)
	# Using shared libraries
	if(DEFINED FFMPEG_BUILDDIR)
		include_directories(ffmpeg ${FFMPEG_BUILDDIR})

		add_library(libavformat STATIC IMPORTED)
		set_target_properties(libavformat PROPERTIES IMPORTED_LOCATION ${FFMPEG_BUILDDIR}/libavformat/libavformat.a)
		add_library(libavcodec STATIC IMPORTED)
		set_target_properties(libavcodec PROPERTIES IMPORTED_LOCATION ${FFMPEG_BUILDDIR}/libavcodec/libavcodec.a)
		add_library(libavutil STATIC IMPORTED)
		set_target_properties(libavutil PROPERTIES IMPORTED_LOCATION ${FFMPEG_BUILDDIR}/libavutil/libavutil.a)
		add_library(libswresample STATIC IMPORTED)
		set_target_properties(libswresample PROPERTIES IMPORTED_LOCATION ${FFMPEG_BUILDDIR}/libswresample/libswresample.a)
		add_library(libswscale STATIC IMPORTED)
		set_target_properties(libswscale PROPERTIES IMPORTED_LOCATION ${FFMPEG_BUILDDIR}/libswscale/libswscale.a)

		SET (FFMPEG_LIBRARIES
			libavformat
			libavcodec
			libavutil
			libswresample
			libswscale
		)
	endif()

	if(IOS OR BLACKBERRY)
		set(FFMPEG_LIBRARIES ${FFMPEG_LIBRARIES} iconv)
	endif()

	if(APPLE)
		set(FFMPEG_LIBRARIES ${FFMPEG_LIBRARIES} bz2 "-framework CoreVideo")
		if (NOT IOS)
			set(FFMPEG_LIBRARIES ${FFMPEG_LIBRARIES} "-framework VideoDecodeAcceleration")
		endif()
	endif(APPLE)

	set(LinkCommon ${LinkCommon} ${FFMPEG_LIBRARIES})
	add_definitions(-DUSE_FFMPEG)
endif(USE_FFMPEG)

# Modification to show where we are pulling the ffmpeg libraries from.
if(USE_FFMPEG AND DEFINED FFMPEG_LIBRARIES)
	target_link_libraries(Common ${FFMPEG_LIBRARIES})
	message(STATUS "FFMPEG library locations:")
	if(DEFINED PLATFORM_ARCH)
		set(TEMP ${CMAKE_SOURCE_DIR}/ffmpeg/${PLATFORM_ARCH}/lib)
		message(STATUS "libavcodec location: ${TEMP}/libavcodec.a")
		message(STATUS "libavformat location: ${TEMP}/libavformat.a")
		message(STATUS "libavutil location: ${TEMP}/libavutil.a")
		message(STATUS "libswresample location: ${TEMP}/libswresample.a")
		message(STATUS "libswscale location: ${TEMP}/libswscale.a")
	else()
		get_target_property(TEMP libavcodec IMPORTED_LOCATION)
		message(STATUS "libavcodec location: ${TEMP}")
		get_target_property(TEMP libavformat IMPORTED_LOCATION)
		message(STATUS "libavformat location: ${TEMP}")
		get_target_property(TEMP libavutil IMPORTED_LOCATION)
		message(STATUS "libavutil location: ${TEMP}")
		get_target_property(TEMP libswresample IMPORTED_LOCATION)
		message(STATUS "libswresample location: ${TEMP}")
		get_target_property(TEMP libswscale IMPORTED_LOCATION)
		message(STATUS "libswscale location: ${TEMP}")
	endif(DEFINED PLATFORM_ARCH)
else()
	message(STATUS "ERROR: No FFMPEG library locations")
endif()

if(USE_FFMPEG AND NOT DEFINED FFMPEG_LIBRARIES)
	message(WARNING "FFMPEG_BUILDDIR variable or manual path definition is required to enable FFmpeg. Disabling it.")
	unset(USE_FFMPEG)
endif()

find_package(ZLIB)
if(ZLIB_FOUND)
	include_directories(${ZLIB_INCLUDE_DIR})
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
	native/ext/cityhash/city.cpp
	native/ext/cityhash/city.h
	native/ext/cityhash/citycrc.h
)
include_directories(ext/cityhash)

add_library(libzip STATIC
	native/ext/libzip/zip.h
	native/ext/libzip/mkstemp.c
	native/ext/libzip/zip_add.c
	native/ext/libzip/zip_add_dir.c
	native/ext/libzip/zip_close.c
	native/ext/libzip/zip_delete.c
	native/ext/libzip/zip_dirent.c
	native/ext/libzip/zip_entry_free.c
	native/ext/libzip/zip_entry_new.c
	native/ext/libzip/zip_err_str.c
	native/ext/libzip/zip_error.c
	native/ext/libzip/zip_error_clear.c
	native/ext/libzip/zip_error_get.c
	native/ext/libzip/zip_error_get_sys_type.c
	native/ext/libzip/zip_error_strerror.c
	native/ext/libzip/zip_error_to_str.c
	native/ext/libzip/zip_fclose.c
	native/ext/libzip/zip_file_error_clear.c
	native/ext/libzip/zip_file_error_get.c
	native/ext/libzip/zip_file_get_offset.c
	native/ext/libzip/zip_file_strerror.c
	native/ext/libzip/zip_filerange_crc.c
	native/ext/libzip/zip_fopen.c
	native/ext/libzip/zip_fopen_index.c
	native/ext/libzip/zip_fread.c
	native/ext/libzip/zip_free.c
	native/ext/libzip/zip_get_archive_comment.c
	native/ext/libzip/zip_get_archive_flag.c
	native/ext/libzip/zip_get_file_comment.c
	native/ext/libzip/zip_get_name.c
	native/ext/libzip/zip_get_num_files.c
	native/ext/libzip/zip_memdup.c
	native/ext/libzip/zip_name_locate.c
	native/ext/libzip/zip_new.c
	native/ext/libzip/zip_open.c
	native/ext/libzip/zip_rename.c
	native/ext/libzip/zip_replace.c
	native/ext/libzip/zip_set_archive_comment.c
	native/ext/libzip/zip_set_archive_flag.c
	native/ext/libzip/zip_set_file_comment.c
	native/ext/libzip/zip_set_name.c
	native/ext/libzip/zip_source_buffer.c
	native/ext/libzip/zip_source_file.c
	native/ext/libzip/zip_source_filep.c
	native/ext/libzip/zip_source_free.c
	native/ext/libzip/zip_source_function.c
	native/ext/libzip/zip_source_zip.c
	native/ext/libzip/zip_stat.c
	native/ext/libzip/zip_stat_index.c
	native/ext/libzip/zip_stat_init.c
	native/ext/libzip/zip_strerror.c
	native/ext/libzip/zip_unchange.c
	native/ext/libzip/zip_unchange_all.c
	native/ext/libzip/zip_unchange_archive.c
	native/ext/libzip/zip_unchange_data.c)
target_link_libraries(libzip ${ZLIB_LIBRARY})
include_directories(native/ext/libzip)
set(LIBZIP_LIBRARY libzip)

# FindPNG does a few things we don't want. So do it ourselves.
# Fixed to libpng16, otherwise it can pick up earlier even if newer exists.
find_path(PNG_PNG_INCLUDE_DIR NAMES "libpng16/png.h")
find_library(PNG_LIBRARY NAMES png16 libpng16 )
find_package(PackageHandleStandardArgs)
find_package_handle_standard_args(PNG REQUIRED_VARS PNG_LIBRARY PNG_PNG_INCLUDE_DIR)
if (PNG_FOUND)
	include_directories(${PNG_PNG_INCLUDE_DIR})
else()
	add_library(png16 STATIC
		native/ext/libpng16/pngconf.h
		native/ext/libpng16/pngdebug.h
		native/ext/libpng16/png.c
		native/ext/libpng16/png.h
		native/ext/libpng16/pngerror.c
		native/ext/libpng16/pngget.c
		native/ext/libpng16/pnginfo.h
		native/ext/libpng16/pnglibconf.h
		native/ext/libpng16/pngmem.c
		native/ext/libpng16/pngpread.c
		native/ext/libpng16/pngpriv.h
		native/ext/libpng16/pngread.c
		native/ext/libpng16/pngrio.c
		native/ext/libpng16/pngrtran.c
		native/ext/libpng16/pngrutil.c
		native/ext/libpng16/pngset.c
		native/ext/libpng16/pngstruct.h
		native/ext/libpng16/pngtest.c
		native/ext/libpng16/pngtrans.c
		native/ext/libpng16/pngwio.c
		native/ext/libpng16/pngwrite.c
		native/ext/libpng16/pngwtran.c
		native/ext/libpng16/pngwutil.c)
	set(PNG_LIBRARY png16)
	include_directories(native/ext)
endif()
	
set(nativeExtra)
set(nativeExtraLibs)
if(ANDROID)
	set(nativeExtra ${nativeExtra}
		native/base/NativeApp.h
		native/android/app-android.cpp
		native/android/native_audio.cpp
		native/android/native_audio.h)

	add_library(native_audio SHARED
		native/android/native-audio-so.cpp
		native/android/native-audio-so.h)
	target_link_libraries(native_audio OpenSLES)
	# No target
elseif(IOS)
	set(nativeExtra ${nativeExtra}
		ios/main.mm
		ios/AppDelegate.m
		ios/AppDelegate.h
		ios/ViewController.mm
		ios/ViewController.h
		ios/iOSCoreAudio.cpp
		ios/iOSCoreAudio.h
		ios/iCade/iCadeReaderView.h
		ios/iCade/iCadeReaderView.m
		ios/iCade/iCadeState.h)
	set(nativeExtraLibs ${nativeExtraLibs} "-framework Foundation -framework AudioToolbox -framework CoreGraphics -framework QuartzCore -framework OpenGLES -framework UIKit -framework GLKit -framework OpenAL -weak_framework GameController")
	set(TargetBin PPSSPP)
elseif(USING_QT_UI)
	# Currently unused
	find_package(Qt4 COMPONENTS QtMultimedia QtOpenGL QtGui QtCore)
	include(${QT_USE_FILE})
	qt4_wrap_cpp(nativeQtHeader native/base/QtMain.h)
	set(nativeExtra ${nativeExtra} native/base/QtMain.cpp ${nativeQtHeader})
	set(nativeExtraLibs ${nativeExtraLibs} ${QT_LIBRARIES})
	set(TargetBin PPSSPPQt)
	if(APPLE)
		set(nativeExtraLibs ${nativeExtraLibs} "-framework CoreFoundation")
	endif()
elseif(BLACKBERRY)
	set(nativeExtra ${nativeExtra} native/base/BlackberryMain.cpp native/base/BlackberryDisplay.cpp)
	set(nativeExtraLibs ${nativeExtraLibs} OpenAL bps screen socket EGL)
	set(TargetBin PPSSPPBlackberry)
elseif(SDL_FOUND)
	set(TargetBin PPSSPPSDL)
	# Require SDL
	include_directories(${SDL_INCLUDE_DIR})
	set(nativeExtra ${nativeExtra} 
		SDL/SDLJoystick.h
		SDL/SDLJoystick.cpp
		native/base/PCMain.cpp)
	set(nativeExtraLibs ${nativeExtraLibs} ${SDL_LIBRARY})
	if(APPLE)
		set(nativeExtra ${nativeExtra} SDL/SDLMain.h SDL/SDLMain.mm)
		set(nativeExtraLibs ${nativeExtraLibs} ${COCOA_LIBRARY})
	elseif(PANDORA OR MAEMO)
		set(nativeExtraLibs ${nativeExtraLibs} pthread EGL X11)
	endif()
	set(TargetBin PPSSPPSDL)
else()
	message(FATAL_ERROR "Could not find SDL. Failing.")
endif()

add_library(native STATIC
	${nativeExtra}
	native/base/backtrace.cpp
	native/base/backtrace.h
	native/audio/mixer.cpp
	native/audio/mixer.h
	native/audio/wav_read.cpp
	native/audio/wav_read.h
	native/base/basictypes.h
	native/base/buffer.cpp
	native/base/buffer.h
	native/base/color.h
	native/base/colorutil.cpp
	native/base/colorutil.h
	native/base/display.cpp
	native/base/display.h
	native/base/error_context.cpp
	native/base/error_context.h
	native/base/fastlist.h
	native/base/fastlist_test.cpp
	native/base/functional.h
	native/base/linked_ptr.h
	native/base/logging.h
	native/base/mutex.h
	native/base/scoped_ptr.h
	native/base/stats.h
	native/base/stringutil.cpp
	native/base/stringutil.h
	native/base/timeutil.cpp
	native/base/timeutil.h
	native/data/compression.cpp
	native/data/compression.h
	native/ext/vjson/json.cpp
	native/ext/vjson/json.h
	native/ext/vjson/block_allocator.cpp
	native/ext/vjson/block_allocator.h
	native/file/chunk_file.cpp
	native/file/chunk_file.h
	native/file/dialog.cpp
	native/file/dialog.h
	native/file/easy_file.cpp
	native/file/easy_file.h
	native/file/fd_util.cpp
	native/file/fd_util.h
	native/file/file_util.cpp
	native/file/file_util.h
	native/file/ini_file.cpp
	native/file/ini_file.h
	native/file/path.cpp
	native/file/path.h
	native/file/vfs.h
	native/file/zip_read.cpp
	native/file/zip_read.h
	native/gfx/gl_common.h
	native/gfx/gl_debug_log.cpp
	native/gfx/gl_debug_log.h
	native/gfx/gl_lost_manager.cpp
	native/gfx/gl_lost_manager.h
	native/gfx/texture.cpp
	native/gfx/texture.h
	native/gfx/texture_atlas.cpp
	native/gfx/texture_atlas.h
#	native/gfx/texture_dx11.cpp
	native/gfx/texture_gen.cpp
	native/gfx/texture_gen.h
	native/gfx_es2/draw_buffer.cpp
	native/gfx_es2/draw_buffer.h
	native/gfx_es2/draw_text.cpp
	native/gfx_es2/draw_text.h
	native/gfx_es2/fbo.cpp
	native/gfx_es2/fbo.h
	native/gfx_es2/gl_state.cpp
	native/gfx_es2/gl_state.h
	native/gfx_es2/gpu_features.cpp
	native/gfx_es2/gpu_features.h
	native/gfx_es2/glsl_program.cpp
	native/gfx_es2/glsl_program.h
	native/gfx_es2/vertex_format.cpp
	native/gfx_es2/vertex_format.h
	native/gfx_es2/gl3stub.c
	native/gfx_es2/gl3stub.h
	native/i18n/i18n.cpp
	native/i18n/i18n.h
	native/image/png_load.cpp
	native/image/png_load.h
	native/image/zim_load.cpp
	native/image/zim_load.h
	native/image/zim_save.cpp
	native/image/zim_save.h
	native/input/gesture_detector.cpp
	native/input/gesture_detector.h
	native/input/keycodes.h
	native/input/input_state.h
	native/input/input_state.cpp
	native/math/curves.cpp
	native/math/curves.h
	native/math/expression_parser.cpp
	native/math/expression_parser.h
	native/math/lin/aabb.cpp
	native/math/lin/aabb.h
	native/math/lin/matrix4x4.cpp
	native/math/lin/matrix4x4.h
	native/math/lin/plane.cpp
	native/math/lin/plane.h
	native/math/lin/quat.cpp
	native/math/lin/quat.h
	native/math/lin/ray.h
	native/math/lin/vec3.cpp
	native/math/lin/vec3.h
	native/math/math_util.cpp
	native/math/math_util.h
	native/net/http_client.cpp
	native/net/http_client.h
	native/net/resolve.cpp
	native/net/resolve.h
	native/net/url.cpp
	native/net/url.h
	native/profiler/profiler.cpp
	native/profiler/profiler.h
	native/thread/prioritizedworkqueue.cpp
	native/thread/prioritizedworkqueue.h
	native/thread/threadutil.cpp
	native/thread/threadutil.h
	native/thread/threadpool.cpp
	native/thread/threadpool.h
	native/ui/screen.cpp
	native/ui/screen.h
	native/ui/ui.cpp
	native/ui/ui.h
	native/ui/ui_context.cpp
	native/ui/ui_context.h
	native/ui/ui_screen.cpp
	native/ui/ui_screen.h
	native/ui/view.cpp
	native/ui/view.h
	native/ui/viewgroup.cpp
	native/ui/viewgroup.h
	native/ui/virtual_input.cpp
	native/ui/virtual_input.h
	native/util/bits/bits.cpp
	native/util/bits/bits.h
	native/util/bits/hamming.h
	native/util/bits/varint.cpp
	native/util/bits/varint.h
	native/util/hash/hash.cpp
	native/util/hash/hash.h
	native/util/random/perlin.cpp
	native/util/random/perlin.h
	native/util/random/rng.h
	native/util/text/utf8.h
	native/util/text/utf8.cpp
	native/util/text/parsers.h
	native/util/text/parsers.cpp
	native/util/const_map.h
	native/ext/jpge/jpgd.cpp
	native/ext/jpge/jpgd.h
	native/ext/jpge/jpge.cpp
	native/ext/jpge/jpge.h)
include_directories(native)

if(APPLE OR BLACKBERRY)
	SET(RT_LIB)
else()
	SET(RT_LIB rt)
endif()

target_link_libraries(native ${LIBZIP_LIBRARY} ${PNG_LIBRARY} rg_etc1 vjson stb_vorbis snappy ${RT_LIB} ${GLEW_LIBRARIES})

if(ANDROID)
	target_link_libraries(native log)
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
if(ARM)
	set(CoreExtra ${CoreExtra}
		Core/MIPS/ARM/ArmAsm.cpp
		Core/MIPS/ARM/ArmAsm.h
		Core/MIPS/ARM/ArmCompALU.cpp
		Core/MIPS/ARM/ArmCompBranch.cpp
		Core/MIPS/ARM/ArmCompFPU.cpp
		Core/MIPS/ARM/ArmCompLoadStore.cpp
		Core/MIPS/ARM/ArmCompVFPU.cpp
		Core/MIPS/ARM/ArmCompVFPUNEON.cpp
		Core/MIPS/ARM/ArmCompReplace.cpp
		Core/MIPS/ARM/ArmJit.cpp
		Core/MIPS/ARM/ArmJit.h
		Core/MIPS/ARM/ArmRegCache.cpp
		Core/MIPS/ARM/ArmRegCache.h
		Core/MIPS/ARM/ArmRegCacheFPU.cpp
		Core/MIPS/ARM/ArmRegCacheFPU.h
		GPU/GLES/VertexDecoderArm.cpp
		GPU/GLES/VertexDecoder.h
		ext/disarm.cpp)
elseif(X86)
	set(CoreExtra ${CoreExtra}
		Core/MIPS/x86/Asm.cpp
		Core/MIPS/x86/Asm.h
		Core/MIPS/x86/CompALU.cpp
		Core/MIPS/x86/CompBranch.cpp
		Core/MIPS/x86/CompFPU.cpp
		Core/MIPS/x86/CompLoadStore.cpp
		Core/MIPS/x86/CompVFPU.cpp
		Core/MIPS/x86/CompReplace.cpp
		Core/MIPS/x86/Jit.cpp
		Core/MIPS/x86/Jit.h
		Core/MIPS/x86/RegCache.cpp
		Core/MIPS/x86/RegCache.h
		Core/MIPS/x86/RegCacheFPU.cpp
		Core/MIPS/x86/RegCacheFPU.h
		GPU/GLES/VertexDecoderX86.cpp
		GPU/GLES/VertexDecoder.h
		ext/disarm.cpp)
endif()

# Core needs to be linked with dl on these platforms
if(LINUX)
  set(CoreExtraLibs ${CoreExtraLibs} ${CMAKE_DL_LIBS})
endif()

# 'ppsspp_jni' on ANDROID, 'Core' everywhere else
# SHARED on ANDROID, STATIC everywhere else
add_library(${CoreLibName} ${CoreLinkType}
	${CoreExtra}
	Core/Config.cpp
	Core/Config.h
	Core/Core.cpp
	Core/Core.h
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
	Core/HLE/HLETables.cpp
	Core/HLE/HLETables.h
	Core/HLE/KernelWaitHelpers.h
	Core/HLE/__sceAudio.cpp
	Core/HLE/__sceAudio.h
	Core/HLE/sceAtrac.cpp
	Core/HLE/sceAtrac.h
	Core/HLE/sceAudio.cpp
	Core/HLE/sceAudiocodec.cpp
	Core/HLE/sceAudiocodec.h
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
	Core/HLE/sceSsl.cpp
	Core/HLE/sceSsl.h
	Core/HLE/sceUmd.cpp
	Core/HLE/sceUmd.h
	Core/HLE/sceUsb.cpp
	Core/HLE/sceUsb.h
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
	Core/HW/SimpleAT3Dec.cpp
	Core/HW/SimpleAT3Dec.h
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
	Core/Host.cpp
	Core/Host.h
	Core/Loaders.cpp
	Core/Loaders.h
	Core/MIPS/JitCommon/JitCommon.cpp
	Core/MIPS/JitCommon/JitCommon.h
	Core/MIPS/JitCommon/JitBlockCache.cpp
	Core/MIPS/JitCommon/JitBlockCache.h
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
	Core/MIPS/MIPSAsmTables.cpp
	Core/MIPS/MIPSAsmTables.h
	Core/MIPS/MIPSAsm.cpp
	Core/MIPS/MIPSAsm.h
	Core/MemMap.cpp
	Core/MemMap.h
	Core/MemMapFunctions.cpp
	Core/PSPLoaders.cpp
	Core/PSPLoaders.h
	Core/PSPMixer.cpp
	Core/PSPMixer.h
	Core/Reporting.cpp
	Core/Reporting.h
	Core/SaveState.cpp
	Core/SaveState.h
	Core/System.cpp
	Core/System.h
	Core/Util/GameManager.cpp
	Core/Util/GameManager.h
	Core/Util/BlockAllocator.cpp
	Core/Util/BlockAllocator.h
	Core/Util/PPGeDraw.cpp
	Core/Util/PPGeDraw.h
	Core/Util/ppge_atlas.cpp
	Core/Util/ppge_atlas.h
	$<TARGET_OBJECTS:GPU>
	Globals.h
	git-version.cpp)
target_link_libraries(${CoreLibName} Common native kirk cityhash xbrz xxhash
	${CoreExtraLibs} ${GLEW_LIBRARIES} ${OPENGL_LIBRARIES})
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

if(ARMEABI_V7A)
	set(GPU_NEON GPU/Common/TextureDecoderNEON.cpp)
endif()
add_library(GPU OBJECT
	GPU/Common/GPUDebugInterface.h
	GPU/Common/VertexDecoderCommon.cpp
	GPU/Common/VertexDecoderCommon.h
	GPU/Common/IndexGenerator.cpp
	GPU/Common/IndexGenerator.h
	GPU/Common/TextureDecoder.cpp
	GPU/Common/TextureDecoder.h
	${GPU_NEON}
	GPU/Common/PostShader.cpp
	GPU/Common/PostShader.h
	GPU/Common/SplineCommon.h
	GPU/Debugger/Breakpoints.cpp
	GPU/Debugger/Breakpoints.h
	GPU/Debugger/Stepping.cpp
	GPU/Debugger/Stepping.h
	GPU/GLES/GLES_GPU.cpp
	GPU/GLES/GLES_GPU.h
	GPU/GLES/FragmentShaderGenerator.cpp
	GPU/GLES/FragmentShaderGenerator.h
	GPU/GLES/Framebuffer.cpp
	GPU/GLES/Framebuffer.h
	GPU/GLES/ShaderManager.cpp
	GPU/GLES/ShaderManager.h
	GPU/GLES/Spline.cpp
	GPU/GLES/StateMapping.cpp
	GPU/GLES/StateMapping.h
	GPU/GLES/TextureCache.cpp
	GPU/GLES/TextureCache.h
	GPU/GLES/TextureScaler.cpp
	GPU/GLES/TextureScaler.h
	GPU/GLES/TransformPipeline.cpp
	GPU/GLES/TransformPipeline.h
	GPU/GLES/SoftwareTransform.cpp
	GPU/GLES/VertexDecoder.cpp
	GPU/GLES/VertexDecoder.h
	GPU/GLES/VertexShaderGenerator.cpp
	GPU/GLES/VertexShaderGenerator.h
	GPU/GPUInterface.h
	GPU/GeDisasm.cpp
	GPU/GeDisasm.h
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
	GPU/Software/SoftGpu.cpp
	GPU/Software/SoftGpu.h
	GPU/Software/TransformUnit.cpp
	GPU/Software/TransformUnit.h
	GPU/ge_constants.h)
setup_target_project(GPU GPU)

if(WIN32)
	add_executable(PPSSPPWindows WIN32
		Windows/Breakpoints.h
		Windows/DSoundStream.cpp
		Windows/DSoundStream.h
		Windows/Debugger/CPURegsInterface.h
		Windows/Debugger/BreakpointWindow.cpp
		Windows/Debugger/BreakpointWindow.h
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
#		Windows/Debugger/Debugger_Profiler.cpp
#		Windows/Debugger/Debugger_Profiler.h
		Windows/Debugger/Debugger_SymbolMap.h
		Windows/Debugger/Debugger_VFPUDlg.cpp
		Windows/Debugger/Debugger_VFPUDlg.h
		Windows/Debugger/SimpleELF.h
#		Windows/DlgDynaView.cpp
#		Windows/DlgDynaView.h
		Windows/EmuThread.cpp
		Windows/EmuThread.h
		Windows/Globals.cpp
		Windows/InputBox.cpp
		Windows/InputBox.h
		Windows/InputDevice.cpp
		Windows/InputDevice.h
		Windows/KeyboardDevice.cpp
		Windows/KeyboardDevice.h
		Windows/MIPSCompALU.h
		Windows/MIPSCompBranch.h
		Windows/OpenGLBase.cpp
		Windows/OpenGLBase.h
		Windows/W32Util/DialogManager.cpp
		Windows/W32Util/DialogManager.h
		Windows/W32Util/Misc.cpp
		Windows/W32Util/Misc.h
		Windows/W32Util/PropertySheet.cpp
		Windows/W32Util/PropertySheet.h
		Windows/W32Util/ShellUtil.cpp
		Windows/W32Util/ShellUtil.h
		Windows/W32Util/XPTheme.h
		Windows/WindowsFileSystem.h
		Windows/WindowsHost.cpp
		Windows/WindowsHost.h
		Windows/WndMainWindow.cpp
		Windows/WndMainWindow.h
		Windows/XPTheme.h
		Windows/XinputDevice.cpp
		Windows/XinputDevice.h
		Windows/main.cpp
		Windows/main.h
		Windows/ppsspp.rc
		Windows/resource.h
		Windows/stdafx.cpp
		Windows/stdafx.h)
	target_link_libraries(PPSSPPWindows ${CoreLibName}
		kernel32 user32 gdi32 shell32 comctl32 dsound xinput)
	setup_target_project(PPSSPPWindows Windows)
endif()

set(NativeAppSource
	UI/NativeApp.cpp
	UI/DevScreens.cpp
	UI/EmuScreen.cpp
	android/jni/TestRunner.cpp
	UI/GameInfoCache.cpp
	UI/MainScreen.cpp
	UI/MiscScreens.cpp
	UI/GameScreen.cpp
	UI/GameSettingsScreen.cpp
	UI/TiltAnalogSettingsScreen.cpp
	UI/TiltEventProcessor.cpp
	UI/TouchControlLayoutScreen.cpp
	UI/TouchControlVisibilityScreen.cpp
	UI/GamepadEmu.cpp
	UI/UIShader.cpp
	UI/OnScreenDisplay.cpp
	UI/ControlMappingScreen.cpp
	UI/Store.cpp
	UI/CwCheatScreen.cpp
	UI/InstallZipScreen.cpp
	UI/ui_atlas.cpp)
if(ANDROID AND ARM)
	set(NativeAppSource ${NativeAppSource} android/jni/ArmEmitterTest.cpp)
endif()
set(NativeAssets
	android/assets/ui_atlas.zim
	assets/shaders
	assets/ppge_atlas.zim
	assets/langregion.ini
	assets/unknown.png)
set(LinkCommon ${CoreLibName} ${CMAKE_THREAD_LIBS_INIT} ${nativeExtraLibs})


if(HEADLESS)
	add_executable(PPSSPPHeadless
		headless/Headless.cpp
		UI/OnScreenDisplay.cpp
		headless/StubHost.h
		headless/Compare.cpp
		headless/Compare.h)
	target_link_libraries(PPSSPPHeadless
		${COCOA_LIBRARY} ${LinkCommon})
	setup_target_project(PPSSPPHeadless headless)
endif()

if(UNITTEST)
	add_executable(unitTest
		unittest/UnitTest.cpp
	)
	target_link_libraries(unitTest
		${COCOA_LIBRARY} ${LinkCommon})
	setup_target_project(unitTest unittest)
endif()

if (TargetBin)
	if (IOS)
		add_executable(${TargetBin} MACOSX_BUNDLE ${NativeAppSource})
	else()
		add_executable(${TargetBin} ${NativeAppSource})
	endif()
	target_link_libraries(${TargetBin} ${LinkCommon})
endif()

# installs
file(INSTALL ${NativeAssets} DESTINATION assets)
file(INSTALL flash0 lang DESTINATION assets)

# Fix CMake some Xcode settings.
if(APPLE)
	# Fix CMake breaking optimization settings.
	set(CMAKE_CXX_FLAGS_DEBUG "-g -D_DEBUG")
	set(CMAKE_CXX_FLAGS_MINSIZEREL "-Os -D_NDEBUG")
	set(CMAKE_CXX_FLAGS_RELEASE "-O3 -D_NDEBUG")
	set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O2 -g -D_NDEBUG")
	set(CMAKE_C_FLAGS_DEBUG "-g -D_DEBUG")
	set(CMAKE_C_FLAGS_MINSIZEREL "-Os -D_NDEBUG")
	set(CMAKE_C_FLAGS_RELEASE "-O3 -D_NDEBUG")
	set(CMAKE_C_FLAGS_RELWITHDEBINFO "-O2 -g -D_NDEBUG")

	# These can be fast even for debug.
	set_target_properties(cityhash PROPERTIES COMPILE_FLAGS "-O3")
	set_target_properties(snappy PROPERTIES COMPILE_FLAGS "-O3")
	if(NOT ZLIB_FOUND)
		set_target_properties(zlib PROPERTIES COMPILE_FLAGS "-O3")
	endif()
endif()

if(APPLE AND NOT IOS)
	set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -F /Library/Frameworks")
endif()

# packaging and code signing
if(IOS)
	add_dependencies(PPSSPP ${CoreLibName} GPU Common native)

	file(GLOB IOSAssets ios/assets/*.png)
	file(INSTALL ${IOSAssets} DESTINATION assets)
	if (IOS_DEBUG)
		file(INSTALL pspautotests DESTINATION assets)
	endif()
	set(RSRC_XIB_FILES assets/Icon@2x.png)
	set_source_files_properties(${RSRC_XIB_FILES}
		PROPERTIES MACOSX_PACKAGE_LOCATION Resources
	)
	set(APP_DIR_NAME \${TARGET_BUILD_DIR}/\${FULL_PRODUCT_NAME})
	add_custom_command(TARGET PPSSPP POST_BUILD
		COMMAND tar -c -C . --exclude .DS_Store --exclude .git -H assets | tar -x -C '${APP_DIR_NAME}'
	)
	# Force Xcode to relink the binary.
	add_custom_command(TARGET Core PRE_BUILD
		WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
		COMMAND rm -f PPSSPP.build/{Debug,Release,MinSizeRel,RelWithDebInfo}-iphoneos/PPSSPP.build/Objects-normal/*/PPSSPP {Debug,Release,MinSizeRel,RelWithDebInfo}-iphoneos/PPSSPP.app/PPSSPP
	)
	set_target_properties(${TargetBin} PROPERTIES
		MACOSX_BUNDLE_INFO_PLIST "../ios/PPSSPP-Info.plist"
		XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "iPhone/iPad"
	)
endif()

#include(CPack)
