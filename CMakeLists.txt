# vim:noexpandtab:
cmake_minimum_required(VERSION 3.16)
project(PPSSPP)
enable_testing()

#This is supposed to work but doesn't!
if(NOT ANDROID)
	set(CMAKE_CXX_STANDARD 17)
	set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

enable_language(ASM)

add_compile_definitions(__STDC_CONSTANT_MACROS)

# Include AppleClang and Clang.
if(${CMAKE_CXX_COMPILER_ID} STREQUAL "Clang" OR ${CMAKE_CXX_COMPILER_ID} STREQUAL "AppleClang")
	set(CLANG ON)
	message("Clang enabled")
else()
	message("Non-Clang compiler detected: ${CMAKE_CXX_COMPILER_ID}")
endif()

if(FORCED_CPU)
	message("Detected CPU (${CMAKE_SYSTEM_PROCESSOR}) overridden as: ${FORCED_CPU}")
	set(CMAKE_SYSTEM_PROCESSOR ${FORCED_CPU})
endif()

# Detect CPU from CMAKE configuration. Toolchains should set this up
if(CMAKE_SYSTEM_PROCESSOR)
	if(CMAKE_OSX_ARCHITECTURES)
		if("${CMAKE_OSX_ARCHITECTURES}" MATCHES ".*86.*")
			set(X86_DEVICE ON)
			set(X86_64_DEVICE ON)
		endif()
		if("${CMAKE_OSX_ARCHITECTURES}" MATCHES "arm64")
			set(ARM64 ON)
		endif()
	elseif(${CMAKE_SYSTEM_PROCESSOR} MATCHES "^aarch64")
		set(ARM64 ON)
	elseif(${CMAKE_SYSTEM_PROCESSOR} MATCHES "^arm64")
		# M1 Mac
		set(ARM64 ON)
	elseif(${CMAKE_SYSTEM_PROCESSOR} MATCHES "^arm")
		message("ARM_DEVICE is a go")
		set(ARM_DEVICE ON)
		if(UNIX AND NOT APPLE)
			execute_process(COMMAND cat /proc/cpuinfo OUTPUT_VARIABLE OUTSTR)
			string(FIND "${OUTSTR}" "ODROID-XU" pos)
			if(NOT (pos LESS 0))
				if(NOT WEBOS)
					add_compile_options(-mfloat-abi=hard -marm -mtune=cortex-a15.cortex-a7 -mcpu=cortex-a15 -fomit-frame-pointer)
				endif()
				set(ARM_NO_VULKAN ON)
			endif()
		endif()
		if(${CMAKE_SYSTEM_PROCESSOR} MATCHES "^armv7")
			set(ARMV7_DEVICE ON)
			add_compile_options(-mfpu=neon)
			# Horrifying workaround for bug in android cmake stuff for asm files
			if(ANDROID)
				set(CMAKE_ASM_FLAGS "${CMAKE_ASM_FLAGS} -target armv7a-none-linux-android")
			endif()
		endif()
	elseif(${CMAKE_SYSTEM_PROCESSOR} MATCHES "^amd64" OR ${CMAKE_SYSTEM_PROCESSOR} MATCHES "^x86_64" OR ${CMAKE_SYSTEM_PROCESSOR} MATCHES "^AMD64")
		set(X86_DEVICE ON)
		set(X86_64_DEVICE ON)
	elseif(${CMAKE_SYSTEM_PROCESSOR} MATCHES "^x86" OR ${CMAKE_SYSTEM_PROCESSOR} MATCHES "i.86")
		set(X86_DEVICE ON)
	elseif(${CMAKE_SYSTEM_PROCESSOR} MATCHES "^mips")
		set(MIPS_DEVICE ON)
	elseif(${CMAKE_SYSTEM_PROCESSOR} MATCHES "^riscv64")
		set(RISCV64_DEVICE ON)
	elseif(${CMAKE_SYSTEM_PROCESSOR} MATCHES "^loongarch64")
		set(LOONGARCH64_DEVICE ON)
		add_compile_options(-mlsx)
	else()
		message("Unknown CMAKE_SYSTEM_PROCESSOR: ${CMAKE_SYSTEM_PROCESSOR}")
	endif()
endif()


if(NOT ANDROID AND NOT IOS)
	if(ARM_DEVICE OR SIMULATOR)
		set(USING_EGL ON)
	endif()
endif()

if(${CMAKE_SYSTEM_NAME} MATCHES "Linux" AND NOT USE_LIBNX)
	set(LINUX ON)
	add_compile_definitions(__STDC_CONSTANT_MACROS)
endif()

if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
	set(MACOSX ON)
	set(USING_EGL OFF)
endif()

if(${CMAKE_SYSTEM_NAME} MATCHES "Android")
	set(ANDROID ON)
endif()

# We only support Vulkan on Unix, macOS (by MoltenVK), Android and Windows.
if(ANDROID OR WIN32 OR (UNIX AND NOT ARM_NO_VULKAN))
	set(VULKAN ON)
endif()


if(CMAKE_CXX_COMPILER MATCHES "webos" OR
   CMAKE_CXX_COMPILER MATCHES "starfish")
	set(WEBOS ON)
endif()

list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake/Modules)
if(NOT IOS)
	list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake/sdl)
endif()

include(CheckCXXSourceCompiles)
include(GNUInstallDirs)

add_compile_definitions(ASSETS_DIR="${CMAKE_INSTALL_FULL_DATADIR}/ppsspp/assets/")

if(OPENXR AND NOT ARMV7_DEVICE)
	add_compile_definitions(OPENXR)
	add_subdirectory(ext/OpenXR-SDK)
	message("OpenXR enabled")
endif()

# User-editable options (go into CMakeCache.txt)
# :: Processors
option(ARMV7 "Set to ON if targeting an ARMv7 processor" ${ARMV7_DEVICE})
option(ARM "Set to ON if targeting an ARM processor" ${ARM_DEVICE})
option(MIPS "Set to ON if targeting a MIPS processor" ${MIPS_DEVICE})
option(RISCV64 "Set to ON if targeting a RISCV64 processor" ${RISCV64_DEVICE})
option(LOONGARCH64 "Set to ON if targeting a LOONGARCH64 processor" ${LOONGARCH64_DEVICE})
option(X86 "Set to ON if targeting an X86 processor" ${X86_DEVICE})
option(X86_64 "Set to ON if targeting an X86_64 processor" ${X86_64_DEVICE})
# :: Environments
option(USING_EGL "Set to ON if target environment uses EGL" ${USING_EGL})
option(USING_FBDEV "Set to ON if target environment uses fbdev (eg. Pandora)" ${USING_FBDEV})
option(USING_GLES2 "Set to ON if target device uses OpenGL ES 2.0" ${USING_GLES2})
option(USING_X11_VULKAN "Set to OFF if target environment doesn't use X11 for Vulkan" ON)
option(USE_WAYLAND_WSI "Enable or disable Wayland WSI support for Vulkan" ON)
option(USE_VULKAN_DISPLAY_KHR "Enable or disable full screen display of Vulkan" ${USE_VULKAN_DISPLAY_KHR})
# :: Frontends
option(MOBILE_DEVICE "Set to ON when targeting a mobile device" ${MOBILE_DEVICE})
# Set by b.sh for the loongarch64/riscv64 cross builds, whose sysroots have no SDL3.
option(ATLAS_TOOL "Set to OFF to not generate the ATLAS_TOOL target" ${ATLAS_TOOL})
option(UNITTEST "Set to ON to generate the unittest target" ${UNITTEST})
option(SIMULATOR "Set to ON when targeting an x86 simulator of an ARM platform" ${SIMULATOR})
# :: Options
option(USE_LIBNX "Set to ON to build for Switch(libnx)" OFF)
option(USE_FFMPEG "Build with FFMPEG support" ON)
option(USE_SYSTEM_SNAPPY "Dynamically link against system snappy" ${USE_SYSTEM_SNAPPY})
option(USE_SYSTEM_FREETYPE "Dynamically link against system freetype" ${USE_SYSTEM_FREETYPE})
option(USE_SYSTEM_RAPIDJSON "Build using system rapidjson" ${USE_SYSTEM_RAPIDJSON})
option(USE_ASAN "Use address sanitizer" OFF)
option(USE_UBSAN "Use undefined behaviour sanitizer" OFF)
option(USE_CCACHE "Use ccache if detected" ON)
option(USE_NO_MMAP "Disable mmap usage" OFF)

if(USE_CCACHE)
	include(ccache)
endif()

if(USE_SYSTEM_RAPIDJSON)
	add_compile_definitions(SYSTEM_RAPIDJSON)
endif()


if(WEBOS)
	set(USING_X11_VULKAN OFF)
	set(USING_GLES2 ON)
	if(CMAKE_CXX_COMPILER MATCHES "arm")
        set(ARMV7 ON)
	endif()
endif()

if(UNIX AND NOT (APPLE OR ANDROID) AND VULKAN)
	if(USING_X11_VULKAN)
		message("Using X11 for Vulkan")
		find_package(X11)
		include_directories(${X11_Xlib_INCLUDE_PATH})
		add_compile_definitions(VK_USE_PLATFORM_XLIB_KHR)
	else()
		message("NOT using X11 for Vulkan")
	endif()

	# add_compile_definitions(VK_USE_PLATFORM_XCB_KHR)
	find_package(Wayland)
	if(NOT WAYLAND_FOUND)
		message(STATUS "Could not find Wayland libraries, disabling Wayland WSI support for Vulkan.")
	elseif(USE_WAYLAND_WSI)
		include_directories(${WAYLAND_INCLUDE_DIR})
		add_compile_definitions(VK_USE_PLATFORM_WAYLAND_KHR)
	endif()

	if(USE_VULKAN_DISPLAY_KHR)
		message(STATUS "Using experimental full-screen display for Vulkan.")
		add_compile_definitions(VK_USE_PLATFORM_DISPLAY_KHR)
	endif()
endif()


	add_compile_definitions(GLEW_NO_GLU)
	if(NOT MSVC)
		add_compile_options(-fPIC)
	endif()
	add_compile_definitions(HAVE_LIBRETRO_VFS)

if(ANDROID)
	set(MOBILE_DEVICE ON)
	set(USING_GLES2 ON)
endif()

# naett wraps a native HTTPS implementation per platform: WinHTTP on Windows, the Java
# APIs on Android, NSURLSession on macOS, and libcurl elsewhere.
if(WIN32 OR ANDROID OR (APPLE AND NOT IOS))
	# Nothing to do, these need no external library.
else()
	set(HTTPS_NOT_AVAILABLE ON)
endif()

# Made this flag negative because it's hopefully quite temporary and didn't
# want to have to update all build systems.
if(HTTPS_NOT_AVAILABLE)
	add_compile_definitions(HTTPS_NOT_AVAILABLE)
endif()

# Disable the usage of MMAP for the memory system.
# It is not tested on all platforms and can cause issues.
if(USE_NO_MMAP)
	add_compile_definitions(NO_MMAP MASKED_PSP_MEMORY)
endif()

# Work around for some misfeature of the current glslang build system
include_directories(ext/glslang)

# Not sure if this is the best way - what about system glew?
# Anyway, glew will be going away anyway.
include_directories(ext/glew)

if(OPENXR AND NOT ARMV7_DEVICE)
	set(OPENGL_LIBRARIES GLESv3 EGL)
elseif(NOT OPENGL_LIBRARIES AND USING_GLES2)
	set(OPENGL_LIBRARIES GLESv2 EGL)
endif()

if(NOT OPENGL_LIBRARIES)
	if(POLICY CMP0072)
		cmake_policy(SET CMP0072 NEW)
	endif()
	find_package(OpenGL REQUIRED)
endif()

if(USING_EGL)
	if(NOT EGL_LIBRARIES)
		set(EGL_LIBRARIES EGL)
	endif()
	list(APPEND OPENGL_LIBRARIES ${EGL_LIBRARIES})
endif()

set(SDL_LIB_TARGET "")
set(SDL_TTF_LIB_TARGET "")
set(FONTCONFIG_LIB_TARGET "")



include(FindThreads)

if(APPLE)
	find_library(COCOA_LIBRARY Cocoa)
	find_library(IOKIT_LIBRARY IOKit)
	find_library(QUARTZ_CORE_LIBRARY QuartzCore)
endif()

include_directories("${CMAKE_SOURCE_DIR}")
# Common headers use libretro-common (retro_atomic.h and friends), so every target needs it.
include_directories(libretro/libretro-common/include)

if(ANDROID_LEGACY)
	add_compile_definitions(ANDROID_LEGACY)
endif()
if(USING_EGL)
	add_compile_definitions(USING_EGL)
endif()
if(USING_FBDEV)
	add_compile_definitions(USING_FBDEV EGL_NO_X11)
endif()
if(USING_GLES2)
	add_compile_definitions(USING_GLES2)
endif()
if(MOBILE_DEVICE)
	add_compile_definitions(MOBILE_DEVICE)
endif()

if(NOT CMAKE_BUILD_TYPE)
	message(STATUS "No build type selected, default to Release")
	set(CMAKE_BUILD_TYPE "Release")
else()
	message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
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
if(RISCV64)
	message("Generating for RISCV64, ${CMAKE_BUILD_TYPE}")
endif()
if(LOONGARCH64)
	message("Generating for LOONGARCH64, ${CMAKE_BUILD_TYPE}")
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
	include(CheckCXXCompilerFlag)
	# Adds -Wno-<name> only if the compiler knows the warning: older clang (Xcode) rejects new
	# names with a warning on every file. GCC accepts any -Wno-, so test the positive form.
	function(add_no_warning name)
		string(MAKE_C_IDENTIFIER "HAVE_W_${name}" var)
		check_cxx_compiler_flag("-W${name}" ${var})
		if(${var})
			add_compile_options(-Wno-${name})
		endif()
	endfunction()

	add_compile_options(-Wall -Werror=return-type -Wno-unused-function -Wno-sign-compare "$<$<COMPILE_LANGUAGE:CXX>:-Wno-reorder>" -Wno-unknown-pragmas -Wno-unused-value -Wno-unused-variable -Wno-error=incompatible-pointer-types)
	add_no_warning(unused-but-set-variable)
	if(NOT CLANG)
		# This one is very useful but has many false positives.
		add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:-Wno-class-memaccess>")
	else()
		add_compile_options(-Wno-deprecated-declarations -Wno-missing-braces)
	endif()

	if(ANDROID)
		add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:-std=c++17>")
	endif()
	if(CLANG)
		add_compile_options(
			-Wno-nullability-completeness
			-Wno-tautological-pointer-compare
			-Wno-deprecated-register
			-Wno-sign-conversion
			-Wno-shorten-64-to-32
		)
		if(MACOSX OR IOS)
			# Hack around a bad check for __GNUC__ in basis_universal that makes it use old stuff on iOS
			add_no_warning(deprecated-builtins)
		endif()
	endif()

	if(USE_ASAN)
		message("Address sanitizer enabled (DEBUG only)")
		add_compile_options("$<$<CONFIG:Debug>:-fsanitize=address>")
		add_link_options("$<$<CONFIG:Debug>:-fsanitize=address>")
		add_compile_definitions(USE_ASAN)
	endif()
	if(USE_UBSAN)
		message("Undefined behaviour sanitizer enabled (DEBUG only)")
		add_compile_options("$<$<CONFIG:Debug>:-fsanitize=undefined>")
		add_link_options("$<$<CONFIG:Debug>:-fsanitize=undefined>")

		# UBSAN is a collection of sanitizers, including vtpr, which reqiuires RTTI.
		# ext/glslang disables RTTI by default using the `ENABLE_RTTI` option.
		# If RTTI is disabled, we must also disable the vtpr sanitizer.
		if(NOT ENABLE_RTTI)
			add_compile_options("$<$<CONFIG:Debug>:-fno-sanitize=vptr>")
			add_link_options("$<$<CONFIG:Debug>:-fno-sanitize=vptr>")
		endif()
	endif()

	include(CheckSymbolExists)

	add_compile_definitions("$<$<CONFIG:Debug>:_DEBUG>")

	# Enable checking printf-like format strings (also works for logging functions)
	add_compile_options(-Wformat)

	# Disable some warnings
	add_compile_options(-Wno-multichar)

	# Don't compile with strict aliasing, we're not 100% aliasing-safe
	add_compile_options(-fno-strict-aliasing)
	if(${CMAKE_C_COMPILER_ID} STREQUAL "Intel")
		add_compile_options("$<$<CONFIG:Release>:-parallel;-fopenmp>")
	endif()

	add_compile_options(-fno-math-errno)

	if(X86)
		# enable sse2 code generation (enabled by default on X86_64)
		if(NOT MACOSX)
			add_compile_options(-msse2)
		endif()
		if(NOT CLANG)
			add_compile_options(-mfpmath=sse)
			# add_compile_options(-mstackrealign)
		endif()
	endif()

	if(IOS)
		set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0")
	elseif(APPLE AND NOT CMAKE_CROSSCOMPILING)
		# The oldest libc++ in current Xcode supports ("The selected platform is no longer supported" below).
		set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0")
		add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:-stdlib=libc++>" -U__STRICT_ANSI__)
		set(CMAKE_XCODE_ATTRIBUTE_CLANG_CXX_LIBRARY "libc++")
	elseif(NOT ANDROID)
		# TODO: See if we can get rid of no-psabi
		if(NOT ${CMAKE_C_COMPILER_ID} STREQUAL "Intel" AND NOT ${CMAKE_CXX_COMPILER_ID} MATCHES "Clang")
			add_compile_options(-Wno-psabi)
		endif()
		add_compile_definitions(_XOPEN_SOURCE=700)
		add_compile_definitions(_XOPEN_SOURCE_EXTENDED __BSD_VISIBLE=1 _BSD_SOURCE _DEFAULT_SOURCE)
		if(CMAKE_SYSTEM_NAME MATCHES "Linux|SunOS" OR MINGW)
			add_compile_definitions(_LARGEFILE64_SOURCE=1 _FILE_OFFSET_BITS=64)
		endif()
		if(${CMAKE_SYSTEM_NAME} STREQUAL "NetBSD")
			add_compile_definitions(_NETBSD_SOURCE)
		endif()
	elseif(ANDROID)
		add_compile_options(-fsigned-char)
	endif()

	check_symbol_exists(getauxval sys/auxv.h HAVE_GETAUXVAL)
	if(HAVE_GETAUXVAL)
		add_definitions(-DHAVE_GETAUXVAL)
	endif()

	check_symbol_exists(elf_aux_info sys/auxv.h HAVE_ELF_AUX_INFO)
	if(HAVE_ELF_AUX_INFO)
		add_definitions(-DHAVE_ELF_AUX_INFO)
	endif()
else()
	# Disable warnings about MS-specific _s variants of libc functions
	add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
	if (NOT CLANG)
		add_compile_options(-MP)
	endif()
	add_link_options(/NODEFAULTLIB:"libcmt.lib")
endif()

if(WIN32)
	add_compile_definitions(_UNICODE UNICODE)
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

# libretro-common, as one library everything links: the VFS, threading and codecs.
# The unit tests link it as well as the core.
add_library(libretro-common STATIC
	libretro/libretro-common/compat/compat_posix_string.c
	libretro/libretro-common/compat/fopen_utf8.c
	libretro/libretro-common/compat/compat_strl.c
	libretro/libretro-common/encodings/encoding_crc32.c
	libretro/libretro-common/encodings/encoding_deflate.c
	libretro/libretro-common/encodings/encoding_huffman.c
	libretro/libretro-common/encodings/encoding_rzstd.c
	libretro/libretro-common/encodings/encoding_utf.c
	libretro/libretro-common/features/features_cpu.c
	libretro/libretro-common/file/file_path.c
	libretro/libretro-common/file/file_path_io.c
	libretro/libretro-common/formats/7z/r7z_lzma.c
	libretro/libretro-common/formats/chd/rchd.c
	libretro/libretro-common/formats/flac/rflac.c
	libretro/libretro-common/formats/png/rpng.c
	libretro/libretro-common/formats/png/rpng_encode.c
	libretro/libretro-common/formats/zip/rzip_archive.c
	libretro/libretro-common/queues/retro_spsc.c
	libretro/libretro-common/queues/retro_waitable_spsc.c
	libretro/libretro-common/rthreads/retro_asym_eventcount.c
	libretro/libretro-common/rthreads/retro_eventcount.c
	libretro/libretro-common/rthreads/retro_procbarrier.c
	libretro/libretro-common/rthreads/rthreads.c
	libretro/libretro-common/rthreads/tpool.c
	libretro/libretro-common/streams/file_stream.c
	libretro/libretro-common/streams/file_stream_transforms.c
	libretro/libretro-common/streams/interface_stream.c
	libretro/libretro-common/streams/memory_stream.c
	libretro/libretro-common/streams/trans_stream.c
	libretro/libretro-common/streams/trans_stream_deflate.c
	libretro/libretro-common/streams/trans_stream_pipe.c
	libretro/libretro-common/string/rstrtod.c
	libretro/libretro-common/time/rtime.c
	libretro/libretro-common/vfs/vfs_implementation.c)
target_include_directories(libretro-common PUBLIC libretro/libretro-common/include)
target_compile_definitions(libretro-common PUBLIC HAVE_RZSTD HAVE_RFLAC
	HAVE_RCHD_DEFLATE HAVE_RCHD_LZMA HAVE_RCHD_FLAC HAVE_RCHD_ZSTD)
target_link_libraries(libretro-common Threads::Threads)

add_subdirectory(ext)

include_directories(ext/OpenXR-SDK/include)

# For ext/sol includes, that are usually included as sol/*.hpp
include_directories(ext)

include_directories(Common)
if(USING_GLES2 OR (USING_EGL AND NOT USING_FBDEV))
	find_package(X11)
endif()




if(LINUX AND NOT ANDROID)
	set(RT_LIB rt)
endif()

set(ATOMIC_LIB)
if(ANDROID OR (LINUX AND ARM_DEVICE) OR (LINUX AND RISCV64) OR (LINUX AND LOONGARCH64))
	set(ATOMIC_LIB atomic)
endif()

set(GlslangLibs glslang OGLCompiler OSDependent SPIRV spirv-cross-glsl)

if (ENABLE_SPVREMAPPER)
	list(APPEND GlslangLibs SPVRemapper)
endif()

if(WIN32)
	list(APPEND GlslangLibs spirv-cross-hlsl)
endif()

add_subdirectory(Common)

add_subdirectory(GPU)
add_subdirectory(Core)

set(BigFontAssets
	assets/font_atlas.zim
	assets/font_atlas.meta
)

set(NativeAssets
	assets/asciifont_atlas.zim
	assets/asciifont_atlas.meta
	assets/debugger
	assets/upload
	assets/lang
	assets/shaders
	assets/themes
	assets/Roboto_Condensed-Regular.ttf
	assets/Roboto_Condensed-Light.ttf
	assets/Roboto_Condensed-Bold.ttf
	assets/Roboto_Condensed-Italic.ttf
	assets/Inconsolata-Regular.ttf
	assets/compat.ini
	assets/adhoc-servers.json
	assets/infra-dns.json
	assets/gamecontrollerdb.txt
	assets/langregion.ini
	assets/ppge_atlas.zim
	assets/ppge_atlas.meta
	assets/sfx_back.wav
	assets/sfx_confirm.wav
	assets/sfx_select.wav
	assets/sfx_toggle_off.wav
	assets/sfx_toggle_on.wav
	assets/sfx_achievement_unlocked.wav
	assets/sfx_leaderbord_submitted.wav
)

file(GLOB UIImages CONFIGURE_DEPENDS
    assets/ui_images/*.png
    assets/ui_images/*.svg
)


if(UNITTEST)
	add_executable(PPSSPPUnitTest
		unittest/UnitTest.cpp
		unittest/TestShaderGenerators.cpp
		unittest/TestArmEmitter.cpp
		unittest/TestArm64Emitter.cpp
		unittest/TestCrossSIMD.cpp
		unittest/TestIRPassSimplify.cpp
		unittest/TestX64Emitter.cpp
		unittest/TestVertexJit.cpp
		unittest/TestVFS.cpp
		unittest/TestZipSlip.cpp
		unittest/TestLzrc.cpp
		unittest/TestMpegCsc.cpp
		unittest/TestSplineTessellation.cpp
		unittest/TestGEMath.cpp
		unittest/TestDemangle.cpp
		unittest/TestTextureReplacer.cpp
		unittest/TestRiscVEmitter.cpp
		unittest/TestLoongArch64Emitter.cpp
		unittest/TestSoftwareGPUJit.cpp
		unittest/TestThreadManager.cpp
		unittest/TestFFmpegThreading.cpp
		unittest/TestNetWait.cpp
		unittest/JitHarness.cpp
		Core/MIPS/MIPSAsm.cpp
		Core/MIPS/ARM/ArmRegCache.cpp
		Core/MIPS/ARM/ArmRegCacheFPU.cpp
	)
	if(USE_FFMPEG)
		target_compile_definitions(PPSSPPUnitTest PRIVATE TEST_BUNDLED_FFMPEG)
		target_include_directories(PPSSPPUnitTest PRIVATE ${CMAKE_SOURCE_DIR}/ffmpeg)
	endif()

	target_link_libraries(PPSSPPUnitTest ${COCOA_LIBRARY} ${QUARTZ_CORE_LIBRARY} ${IOKIT_LIBRARY} ${CMAKE_THREAD_LIBS_INIT} Common Core)
	if(SDL_LIB_TARGET)
		target_link_libraries(PPSSPPUnitTest ${SDL_LIB_TARGET})
	endif()
	if(SDL_TTF_LIB_TARGET)
		target_link_libraries(PPSSPPUnitTest ${SDL_TTF_LIB_TARGET})
	endif()
	if(FONTCONFIG_LIB_TARGET)
		target_link_libraries(PPSSPPUnitTest ${FONTCONFIG_LIB_TARGET})
	endif()
	if(WIN32)
		target_link_libraries(PPSSPPUnitTest d2d1 dwrite d3d11)
	endif()
	setup_target_project(PPSSPPUnitTest unittest)
	add_test(arm64_emitter PPSSPPUnitTest Arm64Emitter)
	add_test(arm_emitter PPSSPPUnitTest ArmEmitter)
	add_test(x64_emitter PPSSPPUnitTest X64Emitter)
	add_test(vertex_jit PPSSPPUnitTest VertexJit)
	add_test(asin PPSSPPUnitTest Asin)
	add_test(sincos PPSSPPUnitTest SinCos)
	add_test(vfpu_sincos PPSSPPUnitTest VFPUSinCos)
	add_test(math_util PPSSPPUnitTest MathUtil)
	add_test(parsers PPSSPPUnitTest Parsers)
	add_test(jit PPSSPPUnitTest Jit)
	add_test(matrix_transpose PPSSPPUnitTest VFPUMatrixTranspose)
	add_test(parse_lbn PPSSPPUnitTest ParseLBN)
	add_test(quick_texhash PPSSPPUnitTest QuickTexHash)
	add_test(clz PPSSPPUnitTest CLZ)
	add_test(shadergen PPSSPPUnitTest ShaderGenerators)
endif()

if(ATLAS_TOOL)
	if(USE_SYSTEM_FREETYPE)
		find_package(Freetype REQUIRED)
		include_directories(${FREETYPE_INCLUDE_DIRS})
		add_compile_definitions(SYSTEM_FREETYPE)
	else()
		include_directories(ext/freetype/include)
	endif()

	set(AtlasToolSource
		ext/native/tools/atlastool.cpp
	)
	add_executable(AtlasTool ${AtlasToolSource})
	target_link_libraries(AtlasTool ${CMAKE_THREAD_LIBS_INIT} freetype)
	setup_target_project(AtlasTool atlastool)
endif()

	add_subdirectory(libretro)

# installs
if(NOT ANDROID)
	file(INSTALL ${BigFontAssets} DESTINATION assets)
	file(INSTALL ${NativeAssets} DESTINATION assets)
	file(INSTALL ${UIImages} DESTINATION assets/ui_images)
	file(INSTALL assets/flash0 DESTINATION assets)
endif()
