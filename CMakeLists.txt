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

# the libraries in the ffmpeg/ directory are not compatible with mingw
if(MINGW AND NOT USE_SYSTEM_FFMPEG AND NOT BUILD_BUNDLED_FFMPEG)
	set(USE_SYSTEM_FFMPEG ON)
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

if(GOLD)
	add_compile_definitions(GOLD)
	message("Gold Build")
	if(IOS_APP_STORE)
		message("WARNING: Gold build for iOS is deprecated")
	endif()
else()
	message("Non-gold Build")
endif()

if(USE_IAP)
	if(GOLD)
		message(FATAL_ERROR "USE_IAP and GOLD can't be enabled together")
	endif()
	if(NOT IOS_APP_STORE)
		message(FATAL_ERROR "USE_IAP can only be enabled in app store builds")
	endif()
	message("USE_IAP for iOS enabled")
	add_compile_definitions(USE_IAP)
endif()

if(IOS_APP_STORE)
	add_compile_definitions(PPSSPP_PLATFORM_IOS_APP_STORE)
	add_compile_definitions(GLES_SILENCE_DEPRECATION)
	# Set a global default to not generate schemes for each target.
	# Makes using XCode sligthly more sane.
	set(CMAKE_XCODE_GENERATE_SCHEME NO)
	set(CMAKE_XCODE_SCHEME_ENABLE_GPU_API_VALIDATION FALSE)
	set(CMAKE_XCODE_SCHEME_ENABLE_GPU_FRAME_CAPTURE_MODE DISABLED)
	message("iOS App Store build")
elseif(IOS)
	message("iOS sideload build")
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
option(HEADLESS "Set to OFF to not generate the PPSSPPHeadless target" ${HEADLESS})
option(ATLAS_TOOL "Set to OFF to not generate the ATLAS_TOOL target" ${ATLAS_TOOL})
option(UNITTEST "Set to ON to generate the unittest target" ${UNITTEST})
option(SIMULATOR "Set to ON when targeting an x86 simulator of an ARM platform" ${SIMULATOR})
option(LIBRETRO "Set to ON to generate the libretro target" OFF)
# :: Options
option(USE_LIBNX "Set to ON to build for Switch(libnx)" OFF)
option(USE_FFMPEG "Build with FFMPEG support" ON)
option(USE_DISCORD "Build with Discord support" ON)
option(USE_MINIUPNPC "Build with miniUPnPc support" ON)
option(USE_SYSTEM_SNAPPY "Dynamically link against system snappy" ${USE_SYSTEM_SNAPPY})
option(USE_SYSTEM_FFMPEG "Dynamically link against system FFMPEG" ${USE_SYSTEM_FFMPEG})
option(BUILD_BUNDLED_FFMPEG "Build bundled ffmpeg module" ${BUILD_BUNDLED_FFMPEG})
option(USE_SYSTEM_FREETYPE "Dynamically link against system freetype" ${USE_SYSTEM_FREETYPE})
option(USE_SYSTEM_LIBCHDR "Dynamically link against system libchdr" ${USE_SYSTEM_LIBCHDR})
option(USE_SYSTEM_LIBZIP "Dynamically link against system libzip" ${USE_SYSTEM_LIBZIP})

option(USE_SYSTEM_LIBPNG "Dynamically link against system libpng" ON)
option(USE_SYSTEM_RAPIDJSON "Build using system rapidjson" ${USE_SYSTEM_RAPIDJSON})
option(USE_SYSTEM_ZSTD "Dynamically link against system zstd" ${USE_SYSTEM_ZSTD})
option(USE_SYSTEM_MINIUPNPC "Dynamically link against system miniUPnPc" ${USE_SYSTEM_MINIUPNPC})
option(USE_ASAN "Use address sanitizer" OFF)
option(USE_UBSAN "Use undefined behaviour sanitizer" OFF)
option(USE_CCACHE "Use ccache if detected" ON)
option(USE_NO_MMAP "Disable mmap usage" OFF)
option(USE_IAP "IAP enabled" OFF)
option(GOLD "Gold build" OFF)

if(USE_CCACHE)
	include(ccache)
endif()

if(USE_SYSTEM_RAPIDJSON)
	add_compile_definitions(SYSTEM_RAPIDJSON)
endif()

if(USE_SYSTEM_MINIUPNPC)
	add_compile_definitions(USE_SYSTEM_MINIUPNPC)
endif()

if(WEBOS)
	set(USING_X11_VULKAN OFF)
	set(USING_GLES2 ON)
	set(USE_SYSTEM_ZSTD OFF)
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

if(LIBRETRO)
	add_compile_definitions(__LIBRETRO__)
	add_compile_definitions(GLEW_NO_GLU)
	if(NOT MSVC)
		add_compile_options(-fPIC)
	endif()
	add_compile_definitions(HAVE_LIBRETRO_VFS)
endif()

if(ANDROID)
	set(MOBILE_DEVICE ON)
	set(USING_GLES2 ON)
endif()

# naett wraps a native HTTPS implementation per platform: WinHTTP on Windows, the Java
# APIs on Android, NSURLSession on macOS, and libcurl elsewhere.
if(WIN32 OR ANDROID OR (APPLE AND NOT IOS))
	# Nothing to do, these need no external library.
elseif(UNIX AND NOT APPLE AND NOT LIBRETRO)
	# We only need the curl headers at build time - libcurl itself is loaded with dlopen
	# (see ext/naett/src/naett_curl.h), so it stays optional at runtime and a build made
	# here still starts on a machine without it, just without HTTPS.
	find_package(CURL)
	if(NOT CURL_FOUND)
		message(STATUS "libcurl headers not found - building without HTTPS support")
		set(HTTPS_NOT_AVAILABLE ON)
	endif()
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

if(NOT LIBRETRO AND NOT IOS AND NOT LOONGARCH64_DEVICE AND NOT ANDROID)
	find_package(SDL3 QUIET)
	if(NOT SDL3_FOUND)
		if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND (EXISTS "/usr/bin/apt" OR EXISTS "/usr/bin/apt-get" OR EXISTS "/etc/debian_version"))
			message(FATAL_ERROR "SDL3 not found. Install dependencies with: sudo apt install libsdl3-dev libsdl3-ttf-dev")
		else()
			message(FATAL_ERROR "SDL3 not found. Install SDL3 development packages for your distribution and retry.")
		endif()
	endif()
	set(SDL_LIB_TARGET SDL3::SDL3)
	find_package(SDL3_ttf QUIET)
	if(NOT SDL3_ttf_FOUND)
		if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND (EXISTS "/usr/bin/apt" OR EXISTS "/usr/bin/apt-get" OR EXISTS "/etc/debian_version"))
			message(FATAL_ERROR "SDL3_ttf not found. Install dependencies with: sudo apt install libsdl3-ttf-dev")
		else()
			message(FATAL_ERROR "SDL3_ttf not found. Install SDL3_ttf development packages for your distribution and retry.")
		endif()
	endif()
	set(SDL_TTF_LIB_TARGET SDL3_ttf::SDL3_ttf)
	message(STATUS "Using SDL3 and SDL3_ttf")
	add_compile_definitions(USE_SDL3_TTF)
	find_package(Fontconfig QUIET)
	if(Fontconfig_FOUND)
		set(FONTCONFIG_LIB_TARGET Fontconfig::Fontconfig)
		add_compile_definitions(USE_SDL3_TTF_FONTCONFIG)
		message(STATUS "Using Fontconfig for SDL3_ttf fallback fonts")
	else()
		message(WARNING "Fontconfig not found - SDL3_ttf fallback fonts for missing glyphs will be limited")
	endif()
endif()


include(FindThreads)

if(APPLE)
	find_library(COCOA_LIBRARY Cocoa)
	find_library(IOKIT_LIBRARY IOKit)
	find_library(QUARTZ_CORE_LIBRARY QuartzCore)
endif()

include_directories("${CMAKE_SOURCE_DIR}")

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
	# NEON optimizations in libpng17 seem to cause PNG load errors, see #14485.
	add_compile_definitions(PNG_ARM_NEON_OPT=0)

	add_compile_options(-Wall -Werror=return-type -Wno-unused-function -Wno-sign-compare -Wno-unused-but-set-variable "$<$<COMPILE_LANGUAGE:CXX>:-Wno-reorder>" -Wno-unknown-pragmas -Wno-unused-value -Wno-unused-variable -Wno-error=incompatible-pointer-types)
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
			add_compile_options(-Wno-deprecated-builtins)
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
		if(LIBRETRO AND ARM64)
			set(CMAKE_OSX_DEPLOYMENT_TARGET "10.14")
		else()
			set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13")
		endif()
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

add_subdirectory(ext)

include_directories(ext/OpenXR-SDK/include)

# For ext/sol includes, that are usually included as sol/*.hpp
include_directories(ext)

include_directories(Common)
if(USING_GLES2 OR (USING_EGL AND NOT USING_FBDEV))
	find_package(X11)
endif()

if(USE_FFMPEG)
	if(NOT BUILD_BUNDLED_FFMPEG)
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
					if(IOS_PLATFORM STREQUAL "TVOS")
						set(PLATFORM_ARCH "tvos/arm64")
					else()
						set(PLATFORM_ARCH "ios/universal")
					endif()
				elseif(MACOSX)
					set(PLATFORM_ARCH "macosx/universal")
				elseif(LINUX)
					if(ARMV7)
						set(PLATFORM_ARCH "linux/armv7")
					elseif(ARM64)
						set(PLATFORM_ARCH "linux/aarch64")
					elseif(ARM)
						set(PLATFORM_ARCH "linux/arm")
					elseif(MIPS)
						set(PLATFORM_ARCH "linux/mips32")
					elseif(RISCV64)
						set(PLATFORM_ARCH "linux/riscv64")
					elseif(LOONGARCH64)
						set(PLATFORM_ARCH "linux/loongarch64")
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
				if(WEBOS AND ARMV7)
					set(PLATFORM_ARCH "linux/webos")
				endif()
				if(PLATFORM_ARCH)
					set(FFMPEG_DIR "ffmpeg/${PLATFORM_ARCH}")
				else()
					message("Couldn't find an internal FFmpeg build, using system FFmpeg instead")
				endif()
			endif()
		endif()
	
		find_package(FFmpeg REQUIRED avcodec avformat avutil swresample swscale)
		# Check if we need to use avcodec_(alloc|free)_frame instead of av_frame_(alloc|free)
		# Check if we need to use const AVCodec
		set(CMAKE_REQUIRED_INCLUDES ${FFmpeg_INCLUDE_avcodec};${FFmpeg_INCLUDE_avformat})
		set(CMAKE_REQUIRED_LIBRARIES FFmpeg::avcodec;FFmpeg::avformat)
		set(CMAKE_REQUIRED_FLAGS "-pedantic -Wall -Werror -Wno-unused-variable")
		check_cxx_source_compiles("extern \"C\" {
			#include <libavcodec/avcodec.h>
			#include <libavformat/avformat.h>
			}
			static AVCodecContext *s_codec_context = NULL;
			int main() {
				const AVCodec *codec = avcodec_find_encoder(s_codec_context->codec_id);
				return 0;
			}
			" HAVE_LIBAVCODEC_CONST_AVCODEC FAIL_REGEX "invalid conversion")
	
		# Check if we need to use avcodec_alloc_context3 instead of stream->codec
		# Check if we need to use av_frame_get_buffer instead of avcodec_default_get_buffer
	endif()
	if(BUILD_BUNDLED_FFMPEG)
		set(CMAKE_REQUIRED_FLAGS "-pedantic -Wall -Werror -Wno-unused-variable")
	endif()
endif(USE_FFMPEG)

find_package(ZLIB)
if(ZLIB_FOUND AND NOT ANDROID)
	include_directories(${ZLIB_INCLUDE_DIR})
	add_compile_definitions(SHARED_ZLIB)
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

if(NOT MSVC)
	# These can be fast even for debug.
	target_compile_options(udis86 PRIVATE "-O2")
	target_compile_options(cityhash PRIVATE "-O2")
	if(NOT ZLIB_FOUND)
		target_compile_options(zlib PRIVATE "-O2")
	endif()
endif()


find_package(LIBZIP)
if(LIBZIP_FOUND AND USE_SYSTEM_LIBZIP)
	include_directories(${LIBZIP_INCLUDE_DIRS})
	add_compile_definitions(SHARED_LIBZIP)
else()
	add_library(libzip STATIC
		ext/libzip/zip_add.c
		ext/libzip/zip_add_dir.c
		ext/libzip/zip_add_entry.c
		ext/libzip/zip_algorithm_deflate.c
		ext/libzip/zip_buffer.c
		ext/libzip/zip_close.c
		ext/libzip/zip_delete.c
		ext/libzip/zip_dir_add.c
		ext/libzip/zip_dirent.c
		ext/libzip/zip_discard.c
		ext/libzip/zip_entry.c
		ext/libzip/zip_error.c
		ext/libzip/zip_error_clear.c
		ext/libzip/zip_error_get.c
		ext/libzip/zip_error_get_sys_type.c
		ext/libzip/zip_error_strerror.c
		ext/libzip/zip_error_to_str.c
		ext/libzip/zip_extra_field.c
		ext/libzip/zip_extra_field_api.c
		ext/libzip/zip_fclose.c
		ext/libzip/zip_fdopen.c
		ext/libzip/zip_file_add.c
		ext/libzip/zip_file_error_clear.c
		ext/libzip/zip_file_error_get.c
		ext/libzip/zip_file_get_comment.c
		ext/libzip/zip_file_get_external_attributes.c
		ext/libzip/zip_file_get_offset.c
		ext/libzip/zip_file_rename.c
		ext/libzip/zip_file_replace.c
		ext/libzip/zip_file_set_comment.c
		ext/libzip/zip_file_set_encryption.c
		ext/libzip/zip_file_set_external_attributes.c
		ext/libzip/zip_file_set_mtime.c
		ext/libzip/zip_file_strerror.c
		ext/libzip/zip_fopen.c
		ext/libzip/zip_fopen_encrypted.c
		ext/libzip/zip_fopen_index.c
		ext/libzip/zip_fopen_index_encrypted.c
		ext/libzip/zip_fread.c
		ext/libzip/zip_fseek.c
		ext/libzip/zip_ftell.c
		ext/libzip/zip_get_archive_comment.c
		ext/libzip/zip_get_archive_flag.c
		ext/libzip/zip_get_encryption_implementation.c
		ext/libzip/zip_get_file_comment.c
		ext/libzip/zip_get_name.c
		ext/libzip/zip_get_num_entries.c
		ext/libzip/zip_get_num_files.c
		ext/libzip/zip_hash.c
		ext/libzip/zip_io_util.c
		ext/libzip/zip_libzip_version.c
		ext/libzip/zip_memdup.c
		ext/libzip/zip_name_locate.c
		ext/libzip/zip_new.c
		ext/libzip/zip_open.c
		ext/libzip/zip_pkware.c
		ext/libzip/zip_progress.c
		ext/libzip/zip_rename.c
		ext/libzip/zip_replace.c
		ext/libzip/zip_set_archive_comment.c
		ext/libzip/zip_set_archive_flag.c
		ext/libzip/zip_set_default_password.c
		ext/libzip/zip_set_file_comment.c
		ext/libzip/zip_set_file_compression.c
		ext/libzip/zip_set_name.c
		ext/libzip/zip_source_accept_empty.c
		ext/libzip/zip_source_begin_write.c
		ext/libzip/zip_source_begin_write_cloning.c
		ext/libzip/zip_source_buffer.c
		ext/libzip/zip_source_call.c
		ext/libzip/zip_source_close.c
		ext/libzip/zip_source_commit_write.c
		ext/libzip/zip_source_compress.c
		ext/libzip/zip_source_crc.c
		ext/libzip/zip_source_error.c
		ext/libzip/zip_source_file_common.c
		ext/libzip/zip_source_file_stdio.c
		ext/libzip/zip_source_free.c
		ext/libzip/zip_source_function.c
		ext/libzip/zip_source_get_file_attributes.c
		ext/libzip/zip_source_is_deleted.c
		ext/libzip/zip_source_layered.c
		ext/libzip/zip_source_open.c
		ext/libzip/zip_source_pkware_decode.c
		ext/libzip/zip_source_pkware_encode.c
		ext/libzip/zip_source_read.c
		ext/libzip/zip_source_remove.c
		ext/libzip/zip_source_rollback_write.c
		ext/libzip/zip_source_seek.c
		ext/libzip/zip_source_seek_write.c
		ext/libzip/zip_source_stat.c
		ext/libzip/zip_source_supports.c
		ext/libzip/zip_source_tell.c
		ext/libzip/zip_source_tell_write.c
		ext/libzip/zip_source_window.c
		ext/libzip/zip_source_write.c
		ext/libzip/zip_source_zip.c
		ext/libzip/zip_source_zip_new.c
		ext/libzip/zip_stat.c
		ext/libzip/zip_stat_index.c
		ext/libzip/zip_stat_init.c
		ext/libzip/zip_strerror.c
		ext/libzip/zip_string.c
		ext/libzip/zip_unchange.c
		ext/libzip/zip_unchange_all.c
		ext/libzip/zip_unchange_archive.c
		ext/libzip/zip_unchange_data.c
		ext/libzip/zip_utf-8.c
		ext/libzip/zip_err_str.c
	)
	if(WIN32)
		target_compile_options(libzip PRIVATE $<$<C_COMPILER_ID:Clang>:-Wno-incompatible-function-pointer-types> $<$<C_COMPILER_ID:GNU>:-Wno-incompatible-pointer-types>)
		target_sources(libzip PRIVATE
			ext/libzip/zip_source_file_win32.c
			ext/libzip/zip_source_file_win32_named.c
			ext/libzip/zip_source_file_win32_utf16.c
			ext/libzip/zip_source_file_win32_utf8.c
		)
		if(CMAKE_SYSTEM_NAME MATCHES WindowsPhone OR CMAKE_SYSTEM_NAME MATCHES WindowsStore)
			target_sources(libzip PRIVATE ext/libzip/zip_random_uwp.c)
		else()
			target_sources(libzip PRIVATE ext/libzip/zip_source_file_win32_ansi.c ext/libzip/zip_random_win32.c)
		endif()
	else()
		target_sources(libzip PRIVATE
			ext/libzip/zip_mkstempm.c
			ext/libzip/zip_source_file_stdio_named.c
			ext/libzip/zip_random_unix.c
		)
	endif()
	target_compile_definitions(libzip PRIVATE HAVE_STDBOOL_H)
	include_directories(ext/libzip)
	set(LIBZIP_LIBRARY libzip)
endif()

# Android and iOS require at least libpng17.
if(ANDROID OR IOS)
	set(PNG_REQUIRED_VERSION 1.7)
else()
	set(PNG_REQUIRED_VERSION 1.6)
endif()

if(USE_SYSTEM_LIBPNG)
	find_package(PNG ${PNG_REQUIRED_VERSION})
endif()
if(PNG_FOUND)
	include_directories(${PNG_INCLUDE_DIRS})
else()
	if(ARM)
		set(PNG_ARM_INCLUDES
			ext/libpng17/arm/arm_init.c
			ext/libpng17/arm/filter_neon.S
			ext/libpng17/arm/filter_neon_intrinsics.c
		)
	elseif(ARM64)
		set(PNG_ARM_INCLUDES
			ext/libpng17/arm/arm_init.c
			ext/libpng17/arm/filter_neon_intrinsics.c
		)
	endif()
	add_library(png17 STATIC
		ext/libpng17/pngconf.h
		ext/libpng17/pngdebug.h
		ext/libpng17/png.c
		ext/libpng17/png.h
		ext/libpng17/pngerror.c
		ext/libpng17/pngget.c
		ext/libpng17/pnginfo.h
		ext/libpng17/pnglibconf.h
		ext/libpng17/pngmem.c
		ext/libpng17/pngpread.c
		ext/libpng17/pngpriv.h
		ext/libpng17/pngread.c
		ext/libpng17/pngrio.c
		ext/libpng17/pngrtran.c
		ext/libpng17/pngrutil.c
		ext/libpng17/pngset.c
		ext/libpng17/pngstruct.h
		ext/libpng17/pngtrans.c
		ext/libpng17/pngwio.c
		ext/libpng17/pngwrite.c
		ext/libpng17/pngwtran.c
		ext/libpng17/pngwutil.c
		${PNG_ARM_INCLUDES}
	)
	set(PNG_LIBRARIES png17)
	include_directories(ext/libpng17)
endif()


set(targetExtra)
set(targetExtraLibs)

if(IOS OR MACOSX)
	list(APPEND targetExtra
		Common/Render/Text/draw_text_cocoa.mm
		Common/Render/Text/draw_text_cocoa.h)
endif()

if(ANDROID)
	list(APPEND AndroidJniSource
		android/jni/app-android.cpp
		android/jni/AndroidAudio.cpp
		android/jni/AndroidAudio.h
		android/jni/OpenSLContext.cpp
		android/jni/OpenSLContext.h
	)
	# No target
elseif(IOS AND NOT LIBRETRO)
	list(APPEND targetExtra
		ios/main.mm
		ios/AppDelegate.mm
		ios/AppDelegate.h
		ios/SceneDelegate.mm
		ios/SceneDelegate.h
		ios/Controls.h
		ios/Controls.mm
		ios/ViewControllerCommon.h
		ios/ViewControllerCommon.mm
		ios/ViewController.mm
		ios/ViewController.h
		ios/ViewControllerMetal.mm
		ios/ViewControllerMetal.h
		ios/iOSCoreAudio.mm
		ios/iOSCoreAudio.h
		ios/IAPManager.mm
		ios/IAPManager.h
		ios/CameraHelper.mm
		ios/CameraHelper.h
		ios/LocationHelper.mm
		ios/LocationHelper.h
		ios/PPSSPPUIApplication.h
		ios/PPSSPPUIApplication.mm
		ios/SmartKeyboardMap.cpp
		ios/SmartKeyboardMap.hpp
		ios/iCade/iCadeReaderView.h
		ios/iCade/iCadeReaderView.m
		ios/iCade/iCadeState.h
		Common/Battery/AppleBatteryClient.m
	)

	list(APPEND targetExtraLibs "-framework Foundation -framework MediaPlayer -framework AudioToolbox -framework CoreGraphics -framework CoreMotion -framework QuartzCore -framework UIKit -framework GLKit -framework OpenAL -framework AVFoundation -framework CoreLocation -framework CoreText -framework CoreVideo -framework CoreMedia -framework CoreServices -framework Metal -framework IOSurface -framework Photos" )
	if(EXISTS "${CMAKE_IOS_SDK_ROOT}/System/Library/Frameworks/GameController.framework")
		list(APPEND targetExtraLibs "-weak_framework GameController")
	endif()
	if(EXISTS "${CMAKE_IOS_SDK_ROOT}/System/Library/Frameworks/PhotosUI.framework")
		list(APPEND targetExtraLibs "-weak_framework PhotosUI")
	endif()

	if(NOT ICONV_LIBRARY)
		list(APPEND targetExtraLibs iconv)
	endif()

	# TODO: Enable arc globally?
	set_source_files_properties(ios/AppDelegate.mm
		ios/SceneDelegate.mm
		ios/ViewControllerCommon.mm
		ios/ViewController.mm
		ios/ViewControllerMetal.mm
		ios/iOSCoreAudio.mm
		ios/IAPManager.mm
		ios/PPSSPPUIApplication.mm
		ios/iCade/iCadeReaderView.m
		ios/main.mm
		ios/CameraHelper.mm
		ios/LocationHelper.mm
		ios/Controls.mm
		Core/Util/DarwinFileSystemServices.mm
		Common/Battery/AppleBatteryClient.m
		Common/Render/Text/draw_text_cocoa.mm
		PROPERTIES COMPILE_FLAGS -fobjc-arc)

	set(TargetBin PPSSPP)
elseif(IOS AND LIBRETRO)
	list(APPEND targetExtraLibs "-framework GLKit")
elseif(WIN32)
	# Don't care about SDL.
	set(TargetBin PPSSPPWindows)
elseif(LIBRETRO)
elseif(LOONGARCH64_DEVICE)
	# Cross-compiling for loongarch64 currently only builds PPSSPPHeadless, and there's
	# no SDL3 available for that target, so skip the desktop SDL frontend entirely.
else()
	if(GOLD)
		set(TargetBin PPSSPPGold)
	else()
		set(TargetBin PPSSPPSDL)
	endif()
	# Require SDL
	add_compile_definitions(SDL)
	list(APPEND targetExtra
		SDL/SDLJoystick.h
		SDL/SDLJoystick.cpp
		SDL/SDLMain.cpp
		SDL/SDLUtil.cpp
		SDL/SDLGLGraphicsContext.cpp
	)
	if(SDL_TTF_LIB_TARGET)
		list(APPEND targetExtraLibs ${SDL_TTF_LIB_TARGET})
	endif()
	if(FONTCONFIG_LIB_TARGET)
		list(APPEND targetExtraLibs ${FONTCONFIG_LIB_TARGET})
	endif()
	if(APPLE)
		list(APPEND targetExtra
			SDL/SDLCocoaMetalLayer.h
			SDL/SDLCocoaMetalLayer.mm
			SDL/CocoaBarItems.mm
			SDL/CocoaBarItems.h
			SDL/PPSSPPAboutViewController.m
			SDL/PPSSPPAboutViewController.h
			SDL/MacCameraHelper.mm
			Common/Battery/AppleBatteryClient.m
			UI/PSPNSApplicationDelegate.mm
			UI/PSPNSApplicationDelegate.h)

		set_source_files_properties(
			Core/Util/DarwinFileSystemServices.mm
			UI/PSPNSApplicationDelegate.mm
			SDL/SDLCocoaMetalLayer.mm
			SDL/CocoaBarItems.mm
			SDL/PPSSPPAboutViewController.m
			SDL/MacCameraHelper.mm
			Common/Battery/AppleBatteryClient.m
			Common/Render/Text/draw_text_cocoa.mm
			PROPERTIES COMPILE_FLAGS -fobjc-arc)
		list(APPEND targetExtraLibs ${COCOA_LIBRARY} ${QUARTZ_CORE_LIBRARY} ${IOKIT_LIBRARY} "-framework AVFoundation" "-framework CoreMedia" "-framework CoreVideo")
		list(APPEND targetExtraLibs ${SDL_LIB_TARGET})
	else()
		list(APPEND targetExtra
			Common/Render/Text/draw_text_sdl.cpp
			Common/Render/Text/draw_text_sdl.h
		)
		list(APPEND targetExtraLibs ${SDL_LIB_TARGET})
	endif()

	if(USING_EGL AND NOT APPLE)
		list(APPEND targetExtraLibs pthread)
	endif()
endif()

if(NOT LIBRETRO)
	# add_compile_definitions() only affects targets created after the call, and
	# add_subdirectory() snapshots the parent's directory-scope state at the point it's
	# invoked, so this has to happen here rather than inside UI/CMakeLists.txt - see the
	# commit that fixed this same issue for miniUPnPc in Core/CMakeLists.txt.
	if(USE_DISCORD AND NOT IOS)
		add_compile_definitions(USE_DISCORD=1)
	endif()
	add_subdirectory(UI)
endif()

if(ANDROID)
	if(ARM)
		list(APPEND NativeAppSource android/jni/ArmEmitterTest.cpp)
	elseif(ARM64)
		list(APPEND NativeAppSource android/jni/Arm64EmitterTest.cpp)
	endif()

	if (NOT LIBRETRO)
		list(APPEND targetExtra ${NativeAppSource})
	endif()
endif()

if (IOS AND NOT LIBRETRO)
	list(APPEND targetExtra ${NativeAppSource})
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

if(USE_SYSTEM_ZSTD)
	find_package(ZSTD REQUIRED)
else()
	add_subdirectory(ext/zstd-build)
	include_directories(ext/zstd/lib)
endif()

if(USE_SYSTEM_LIBCHDR)
	find_package(PkgConfig)
	if(PkgConfig_FOUND)
		pkg_check_modules(libchdr_PKGCONFIG IMPORTED_TARGET libchdr)
	endif()
else()
	include_directories(ext/libchdr/include)
endif()

add_subdirectory(GPU)
add_subdirectory(Core)

if(ANDROID AND NOT LIBRETRO)
	add_library(ppsspp_jni SHARED ${AndroidJniSource})
	target_link_libraries(ppsspp_jni Core log EGL OpenSLES)
	if(TARGET ppsspp_ui)
		target_link_libraries(ppsspp_jni ppsspp_ui)
	endif()
	if(OPENXR AND NOT ARMV7_DEVICE)
		# Common/VR/*.cpp (part of Common, always linked in via core) calls directly into
		# the OpenXR loader, so ppsspp_jni needs this too - not just ${TargetBin}, which is
		# what openxr_loader was previously only being linked into.
		target_link_libraries(ppsspp_jni openxr_loader)
	endif()
	if(ARM64)
		# Support 16kb page size on Android
		target_link_options(ppsspp_jni PRIVATE "-Wl,-z,max-page-size=16384")
	endif()
endif()

set(WindowsFiles
	Windows/WindowsAudio.cpp
	Windows/WindowsAudio.h
	Windows/WASAPIContext.cpp
	Windows/WASAPIContext.h
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
	Windows/Debugger/DebuggerShared.cpp
	Windows/Debugger/DebuggerShared.h
	Windows/Debugger/Debugger_Disasm.cpp
	Windows/Debugger/Debugger_Disasm.h
	Windows/Debugger/Debugger_MemoryDlg.cpp
	Windows/Debugger/Debugger_MemoryDlg.h
	Windows/Debugger/Debugger_Lists.cpp
	Windows/Debugger/Debugger_Lists.h
	Windows/Debugger/Debugger_VFPUDlg.cpp
	Windows/Debugger/Debugger_VFPUDlg.h
	Windows/Debugger/WatchItemWindow.cpp
	Windows/Debugger/WatchItemWindow.h
	Windows/Debugger/EditSymbolsWindow.cpp
	Windows/Debugger/EditSymbolsWindow.h
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
	Windows/BufferLock.h
	Windows/CaptureDevice.cpp
	Windows/CaptureDevice.h
	Windows/DinputDevice.cpp
	Windows/DinputDevice.h
	Windows/Hid/HidCommon.h
	Windows/Hid/HidCommon.cpp
	Windows/Hid/HidInputDevice.h
	Windows/Hid/HidInputDevice.cpp
	Windows/Hid/DualSense.h
	Windows/Hid/DualSense.cpp
	Windows/Hid/DualShock.h
	Windows/Hid/DualShock.cpp
	Windows/Hid/SwitchPro.h
	Windows/Hid/SwitchPro.cpp
	Windows/GPU/D3D11Context.cpp
	Windows/GPU/D3D11Context.h
	Windows/GPU/WindowsGLContext.cpp
	Windows/InputBox.cpp
	Windows/InputBox.h
	Windows/InputDevice.cpp
	Windows/InputDevice.h
	Windows/W32Util/ContextMenu.h
	Windows/W32Util/ContextMenu.h
	Windows/W32Util/DialogManager.cpp
	Windows/W32Util/DialogManager.h
	Windows/W32Util/Misc.cpp
	Windows/W32Util/Misc.h
	Windows/W32Util/ShellUtil.cpp
	Windows/W32Util/ShellUtil.h
	Windows/W32Util/TabControl.cpp
	Windows/W32Util/TabControl.h
	Windows/W32Util/IatHook.h
	Windows/W32Util/ContextMenu.h
	Windows/W32Util/ContextMenu.cpp
	Windows/W32Util/DarkMode.h
	Windows/W32Util/DarkMode.cpp
	Windows/W32Util/UAHMenuBar.h
	Windows/W32Util/UAHMenuBar.cpp
	Windows/MainWindow.cpp
	Windows/MainWindow.h
	Windows/MainWindowMenu.cpp
	Windows/MainWindowMenu.h
	Windows/RawInput.cpp
	Windows/RawInput.h
	Windows/TouchInputHandler.cpp
	Windows/TouchInputHandler.h
	Windows/XinputDevice.cpp
	Windows/XinputDevice.h
	Windows/main.cpp
	Windows/main.h
	Windows/ppsspp.rc
	Windows/resource.h
	Windows/stdafx.cpp
	Windows/stdafx.h
)

if(WIN32)
	#setup_target_project(${TargetBin} Windows)
	list(APPEND NativeAppSource ${WindowsFiles})
endif()

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
	assets/vfpu
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

if(HEADLESS)
	set(HeadlessSource
		headless/Headless.cpp
		headless/Compare.cpp
		headless/Compare.h
	)
	if(SDL_LIB_TARGET)
		list(APPEND HeadlessSource
			headless/SDLHeadlessGLGraphicsContext.cpp
			headless/SDLHeadlessGLGraphicsContext.h
			# TODO: Break these out into a library instead of cross linking like this.
			SDL/SDLUtil.cpp
			SDL/SDLUtil.h
		)
	endif()
	if(APPLE)
		list(APPEND HeadlessSource
			Common/Render/Text/draw_text_cocoa.mm
			Common/Render/Text/draw_text_cocoa.h
			SDL/SDLCocoaMetalLayer.h
			SDL/SDLCocoaMetalLayer.mm
		)
		set_source_files_properties(Common/Render/Text/draw_text_cocoa.mm
			PROPERTIES COMPILE_FLAGS -fobjc-arc)
	elseif(SDL_TTF_LIB_TARGET AND NOT WIN32)
		list(APPEND HeadlessSource
			Common/Render/Text/draw_text_sdl.cpp
			Common/Render/Text/draw_text_sdl.h
		)
	endif()
	if(WIN32)
		list(APPEND HeadlessSource
			headless/WindowsHeadlessHost.cpp
			headless/WindowsHeadlessHost.h
			Windows/GPU/D3D11Context.cpp
			Windows/GPU/D3D11Context.h
			Windows/GPU/WindowsGLContext.cpp
			Windows/W32Util/ShellUtil.cpp
			Windows/W32Util/ShellUtil.h
			Windows/CaptureDevice.cpp
			Windows/CaptureDevice.h
			Windows/W32Util/Misc.cpp
			Windows/W32Util/Misc.h
		)
	endif()
	add_executable(PPSSPPHeadless ${HeadlessSource})
	target_link_libraries(PPSSPPHeadless ${COCOA_LIBRARY} ${QUARTZ_CORE_LIBRARY} ${IOKIT_LIBRARY} ${CMAKE_THREAD_LIBS_INIT} Core)
	if(SDL_LIB_TARGET)
		target_link_libraries(PPSSPPHeadless ${SDL_LIB_TARGET})
	endif()
	if(SDL_TTF_LIB_TARGET)
		target_link_libraries(PPSSPPHeadless ${SDL_TTF_LIB_TARGET})
	endif()
	if(FONTCONFIG_LIB_TARGET)
		target_link_libraries(PPSSPPHeadless ${FONTCONFIG_LIB_TARGET})
	endif()
	if(WIN32)
		target_link_libraries(PPSSPPHeadless d2d1 dwrite d3d11)
	endif()
	setup_target_project(PPSSPPHeadless headless)
endif()

if(UNITTEST)
	add_executable(PPSSPPUnitTest
		unittest/UnitTest.cpp
		unittest/TestShaderGenerators.cpp
		unittest/TestArmEmitter.cpp
		unittest/TestArm64Emitter.cpp
		unittest/TestIRPassSimplify.cpp
		unittest/TestX64Emitter.cpp
		unittest/TestVertexJit.cpp
		unittest/TestVFS.cpp
		unittest/TestZipSlip.cpp
		unittest/TestLzrc.cpp
		unittest/TestDemangle.cpp
		unittest/TestTextureReplacer.cpp
		unittest/TestRiscVEmitter.cpp
		unittest/TestLoongArch64Emitter.cpp
		unittest/TestSoftwareGPUJit.cpp
		unittest/TestThreadManager.cpp
		unittest/JitHarness.cpp
		Core/MIPS/ARM/ArmRegCache.cpp
		Core/MIPS/ARM/ArmRegCacheFPU.cpp
	)
	if(APPLE)
		target_sources(PPSSPPUnitTest PRIVATE
			Common/Render/Text/draw_text_cocoa.mm
			Common/Render/Text/draw_text_cocoa.h
		)
		set_source_files_properties(Common/Render/Text/draw_text_cocoa.mm
			PROPERTIES COMPILE_FLAGS -fobjc-arc)
	elseif(SDL_TTF_LIB_TARGET AND NOT WIN32)
		target_sources(PPSSPPUnitTest PRIVATE
			Common/Render/Text/draw_text_sdl.cpp
			Common/Render/Text/draw_text_sdl.h
		)
	endif()
	if(WIN32)
		target_sources(PPSSPPUnitTest PRIVATE
			Windows/CaptureDevice.cpp
			Windows/CaptureDevice.h
		)
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

if(LIBRETRO)
	add_subdirectory(libretro)
endif()

if(TargetBin)
	if(APPLE)
		if(NOT IOS)
			if(GOLD)
				set(ICON_PATH_ABS ${CMAKE_CURRENT_SOURCE_DIR}/icons/ppsspp_gold.icns)
				set(MACOSX_BUNDLE_ICON_FILE ppsspp_gold.icns)
				set(MACOSX_BUNDLE_BUNDLE_NAME "PPSSPP Gold")
				set(MACOSX_BUNDLE_GUI_IDENTIFIER org.ppsspp.ppssppgold)
			else()
				set(ICON_PATH_ABS ${CMAKE_CURRENT_SOURCE_DIR}/icons/ppsspp.icns)
				set(MACOSX_BUNDLE_ICON_FILE ppsspp.icns)
				set(MACOSX_BUNDLE_BUNDLE_NAME "PPSSPP")
				set(MACOSX_BUNDLE_GUI_IDENTIFIER org.ppsspp.ppsspp)
			endif()
			set_source_files_properties(${ICON_PATH_ABS} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
		endif()

		# TODO: there must a native way to copy these.
		# Now this is very prone to errors when changes occur.
		# Also better to have assets under Resources dir for OS X.
		file(GLOB_RECURSE FLASH0_FILES assets/flash0/*)
		file(GLOB_RECURSE LANG_FILES assets/lang/*)
		file(GLOB_RECURSE SHADER_FILES assets/shaders/*)
		file(GLOB_RECURSE THEME_FILE assets/themes/*)
		file(GLOB_RECURSE DEBUGGER_FILES assets/debugger/*)
		file(GLOB_RECURSE WEB_UPLOAD_FILES assets/upload/*)
		file(GLOB_RECURSE VFPU_FILES assets/vfpu/*)

		if(NOT IOS)
			set_source_files_properties(${BigFontAssets} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/assets")
			set_source_files_properties(${NativeAssets} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/assets")
			set_source_files_properties(${UIImages} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/assets/ui_images")
			set_source_files_properties(${FLASH0_FILES} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/assets/flash0/font")
			set_source_files_properties(${LANG_FILES} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/assets/lang")
			set_source_files_properties(${SHADER_FILES} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/assets/shaders")
			set_source_files_properties(${THEME_FILE} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/assets/themes")
			set_source_files_properties(${DEBUGGER_FILES} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/assets/debugger")
			set_source_files_properties(${WEB_UPLOAD_FILES} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/assets/upload")
			set_source_files_properties(${VFPU_FILES} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/assets/vfpu")
		endif()

		if(IOS)
			set(AssetCatalog "${CMAKE_SOURCE_DIR}/ios/assets.xcassets")
			add_executable(${TargetBin} MACOSX_BUNDLE ${NativeAssets} ${BigFontAssets} ${UIImages} ${AssetCatalog} ${SHADER_FILES} ${THEME_FILE} ${DEBUGGER_FILES} ${WEB_UPLOAD_FILES} ${FLASH0_FILES} ${LANG_FILES} ${NativeAppSource} "ios/Settings.bundle" "ios/Launch Screen.storyboard")
			if(NOT IOS_APP_STORE)
				file(INSTALL "${CMAKE_SOURCE_DIR}/ext/vulkan/iOS/Frameworks/libMoltenVK.dylib" DESTINATION "${CMAKE_BINARY_DIR}/PPSSPP.app/Frameworks/")
			endif()
		else()
			add_executable(${TargetBin} MACOSX_BUNDLE ${ICON_PATH_ABS} ${NativeAssets} ${BigFontAssets} ${UIImages} ${SHADER_FILES} ${THEME_FILE} ${DEBUGGER_FILES} ${WEB_UPLOAD_FILES} ${FLASH0_FILES} ${LANG_FILES} ${NativeAppSource})
			file(INSTALL "${CMAKE_SOURCE_DIR}/ext/vulkan/macOS/Frameworks/libMoltenVK.dylib" DESTINATION "${CMAKE_BINARY_DIR}/${TargetBin}.app/Contents/Frameworks/")
		endif()
	elseif(WIN32)
		add_executable(${TargetBin} WIN32 ${NativeAppSource})
		if(NOT MSVC)
			target_compile_options(${TargetBin} PRIVATE $<$<CXX_COMPILER_ID:Clang>:-Wno-c++11-narrowing>)
		endif()
		target_link_libraries(${TargetBin} avrt comctl32 dinput8 dwmapi hid ksuser mfplat setupapi uxtheme version)
		set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${TargetBin})
	else()
		add_executable(${TargetBin} ${NativeAppSource})
	endif()
	if(targetExtra)
		target_sources(${TargetBin} PRIVATE ${targetExtra})
	endif()
	if(ANDROID AND ARM64)
		target_link_options(${TargetBin} PRIVATE "-Wl,-z,max-page-size=16384")
	endif()
	if(TARGET ppsspp_ui)
		target_link_libraries(${TargetBin} ${CMAKE_THREAD_LIBS_INIT} ${targetExtraLibs} ppsspp_ui)
	else()
		target_link_libraries(${TargetBin} ${CMAKE_THREAD_LIBS_INIT} ${targetExtraLibs} Core)
	endif()
endif()

if(TargetBin)
	if(ANDROID)
		target_link_libraries(${TargetBin} log EGL OpenSLES)
	elseif(WIN32)
		target_link_libraries(${TargetBin} ws2_32 winmm)
	elseif(${CMAKE_SYSTEM_NAME} MATCHES "^(DragonFly|FreeBSD|NetBSD)$")
		target_link_libraries(${TargetBin} execinfo)
	endif()
endif()

# installs
if(NOT ANDROID)
	file(INSTALL ${BigFontAssets} DESTINATION assets)
	file(INSTALL ${NativeAssets} DESTINATION assets)
	file(INSTALL ${UIImages} DESTINATION assets/ui_images)
	file(INSTALL assets/flash0 DESTINATION assets)
endif()
# packaging and code signing
if(IOS AND NOT LIBRETRO)
	if(IOS_APP_STORE)
		set(DEPLOYMENT_TARGET 13.0)
	else()
		set(DEPLOYMENT_TARGET 11.0)
	endif()
	file(GLOB IOSAssets ios/assets/*.png)
	list(REMOVE_ITEM IOSAssets ${CMAKE_CURRENT_SOURCE_DIR}/ios/assets/Default-568h@2x.png)
	list(REMOVE_ITEM IOSAssets ${CMAKE_CURRENT_SOURCE_DIR}/ios/assets/Default-568h@3x.png)
	file(INSTALL ${IOSAssets} DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/assets)
	file(GLOB IOSAssets ios/assets/Default-568h@*.png)
	file(INSTALL ${IOSAssets} DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
	if(IOS_DEBUG)
		file(INSTALL pspautotests DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/assets)
	endif()
	set(RSRC_XIB_FILES "Launch Screen.storyboard" ${CMAKE_CURRENT_SOURCE_DIR}/ios/assets.xcassets)

	set_source_files_properties(${RSRC_XIB_FILES}
		PROPERTIES MACOSX_PACKAGE_LOCATION Resources
	)

	#This breaks in modern XCode. Not sure when it worked...
	#if(CMAKE_GENERATOR STREQUAL "Xcode")
	#	set(APP_DIR_NAME "$(TARGET_BUILD_DIR)/$(FULL_PRODUCT_NAME)")
	#else()
	set(APP_DIR_NAME "$<TARGET_FILE_DIR:PPSSPP>")
	#endif()

	set(MACOSX_DEPLOYMENT_TARGET ${DEPLOYMENT_TARGET})
	set(PRODUCT_NAME "PPSSPP")
	set(BUNDLE_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/ios/PPSSPP-Info.plist")
	set(BUNDLE_ENTITLEMENTS "${CMAKE_CURRENT_SOURCE_DIR}/ios/App.entitlements")
	if(GOLD)
		if(IOS_APP_STORE)
			set(BUNDLE_IDENTIFIER "org.ppsspp.ppsspp-gold")
		else()
			set(BUNDLE_IDENTIFIER "org.ppsspp.ppssppgold")
		endif()
		set(ICON_NAME "PPSSPPGold")
		set(DISPLAY_NAME "PPSSPP Gold")
	else()
		if(IOS_APP_STORE)
			set(BUNDLE_IDENTIFIER "org.ppsspp.ppsspp-free")
		else()
			set(BUNDLE_IDENTIFIER "org.ppsspp.ppsspp")
		endif()
		set(ICON_NAME "AppIcon")
		set(DISPLAY_NAME "PPSSPP")
	endif()
	if(IOS_APP_STORE)
		message(STATUS "DevTeam: ${DEVELOPMENT_TEAM_ID} Icon: ${ICON_NAME} Target: ${TargetBin} Gold: ${GOLD} IAP: ${USE_IAP}")
		message(STATUS "CURRENT_BINARY_DIR: ${CMAKE_CURRENT_BINARY_DIR}")

		# This is for injecting the version into the plist, and also copying resources.
		# Should find a different way to do both these things.
		add_custom_command(TARGET ${TargetBin} POST_BUILD
			COMMAND echo "Creating ${APP_DIR_NAME} for app store build"
			COMMAND echo "CURRENT_BINARY_DIR: ${CMAKE_CURRENT_BINARY_DIR}"
			COMMAND echo "BINARY_DIR: ${CMAKE_BINARY_DIR}"
			COMMAND mkdir -p \"${APP_DIR_NAME}\"
				# This tar command seems to be responsible for copying assets. I thought we had another step that did that..
				# Prepend -v to the extracting command to see the files copied.
			COMMAND tar -c -C ${CMAKE_CURRENT_BINARY_DIR} --exclude .DS_Store --exclude .git assets *.png | tar -x -C \"${APP_DIR_NAME}\"
				# This updates the version in the plist.
			COMMAND /bin/bash "${CMAKE_SOURCE_DIR}/ios/iosbundle.sh" \"${APP_DIR_NAME}\" "${CMAKE_CURRENT_BINARY_DIR}"
		)

		# Can't figure out properly using .xcframework from CMake, so just linking directly to the .a file.
		target_link_libraries(${TargetBin}
			"${CMAKE_CURRENT_SOURCE_DIR}/ios/MoltenVK/MoltenVK.xcframework/ios-arm64/libMoltenVK.a"
		)
		# https://stackoverflow.com/questions/40664125/cmake-and-code-signing-in-xcode-8-for-ios-projects
		target_compile_options(${TargetBin} PRIVATE
			$<$<CONFIG:Release>:-g -gline-tables-only>
		)
		target_compile_options(${TargetBin} PRIVATE
			$<$<CONFIG:RelWithDebInfo>:-g>
		)
		set_target_properties(${TargetBin} PROPERTIES
			XCODE_GENERATE_SCHEME YES  # Avoid the scheme bloat in XCode by only setting it to YES for this target.
			RESOURCE "ios/Launch Screen.storyboard"
			RESOURCE "ios/Settings.bundle"
			RESOURCE "ios/assets.xcassets"
			XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER ${BUNDLE_IDENTIFIER}
			XCODE_ATTRIBUTE_PRODUCT_NAME ${PRODUCT_NAME}
			XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_APPICON_NAME ${ICON_NAME}
			BUILD_WITH_INSTALL_RPATH YES
			MACOSX_BUNDLE_INFO_PLIST ${BUNDLE_PLIST}
			# Some sources say we should generate the PLIST. There's stuff in it that
			# I don't know how to generate, though.
			#XCODE_ATTRIBUTE_GENERATE_INFOPLIST_FILE "YES"
			#XCODE_ATTRIBUTE_INFOPLIST_KEY_UIRequiredDeviceCapabilities arm64
			XCODE_ATTRIBUTE_INFOPLIST_KEY_UIFileSharingEnabled YES
			XCODE_EMBED_FRAMEWORKS_REMOVE_HEADERS_ON_COPY YES
			XCODE_EMBED_FRAMEWORKS_CODE_SIGN_ON_COPY YES
			XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET ${DEPLOYMENT_TARGET}
			XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
			XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC YES
			XCODE_ATTRIBUTE_ENABLE_BITCODE NO
			XCODE_ATTRIBUTE_DEVELOPMENT_TEAM ${DEVELOPMENT_TEAM_ID}
			XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "Apple Development"
			XCODE_ATTRIBUTE_CODE_SIGN_STYLE "Automatic"
			XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_INCLUDE_ALL_APPICON_ASSETS YES

			XCODE_ATTRIBUTE_DEBUG_INFORMATION_FORMAT "dwarf-with-dsym"
			XCODE_ATTRIBUTE_DEPLOYMENT_POSTPROCESSING[variant=Release] "YES"
			XCODE_ATTRIBUTE_STRIP_INSTALLED_PRODUCT[variant=Release] "YES"

			XCODE_ATTRIBUTE_SKIP_INSTALL NO
			XCODE_ATTRIBUTE_INSTALL_PATH "$(LOCAL_APPS_DIR)"
		)
	else()
		# This is for injecting the version into the plist.
		add_custom_command(TARGET PPSSPP POST_BUILD
			COMMAND echo "Creating ${APP_DIR_NAME} for sideload build"
			COMMAND mkdir -p \"${APP_DIR_NAME}\"
			COMMAND tar -c -C ${CMAKE_CURRENT_BINARY_DIR} --exclude .DS_Store --exclude .git assets *.png | tar -x -C \"${APP_DIR_NAME}\"
			COMMAND /bin/bash "${CMAKE_SOURCE_DIR}/ios/macbundle.sh" \"${APP_DIR_NAME}\"
		)

		set_target_properties(${TargetBin} PROPERTIES
			MACOSX_BUNDLE_INFO_PLIST ${BUNDLE_PLIST}
			XCODE_GENERATE_SCHEME YES
			RESOURCE "ios/Launch Screen.storyboard"
			RESOURCE "ios/Settings.bundle"
			RESOURCE "ext/vulkan/iOS/Frameworks"
			XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_APPICON_NAME ${ICON_NAME}
			XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET ${DEPLOYMENT_TARGET}
			XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
			XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC YES
			XCODE_ATTRIBUTE_ENABLE_BITCODE NO
			XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "-"
		)
	endif()
	add_custom_command(TARGET PPSSPP POST_BUILD
		COMMAND plutil -replace CFBundleDisplayName -string "${DISPLAY_NAME}" "${APP_DIR_NAME}/Info.plist"
		COMMAND plutil -replace CFBundleIdentifier -string "${BUNDLE_IDENTIFIER}" "${APP_DIR_NAME}/Info.plist"
	)
endif()

if(MACOSX AND NOT IOS)
	if(GOLD)
		set_target_properties(${TargetBin} PROPERTIES
			MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/macOS/InfoGold.plist"
		)
	else()
		set_target_properties(${TargetBin} PROPERTIES
			MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/macOS/Info.plist"
		)
	endif()
endif()

if(UNIX AND NOT ANDROID AND NOT APPLE)
	configure_file(
		"${CMAKE_SOURCE_DIR}/ppsspp.desktop.in"
		"${CMAKE_BINARY_DIR}/ppsspp.desktop"
		@ONLY
	)
	install(
		TARGETS ${TargetBin}
		DESTINATION "${CMAKE_INSTALL_BINDIR}"
	)
	install(
		DIRECTORY "${CMAKE_BINARY_DIR}/assets"
		DESTINATION "${CMAKE_INSTALL_DATADIR}/ppsspp"
		PATTERN ".git*" EXCLUDE
		PATTERN "mime" EXCLUDE
		PATTERN "lang/README.md" EXCLUDE
	)
	install(
		FILES "${CMAKE_BINARY_DIR}/ppsspp.desktop"
		DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/applications"
		RENAME ${TargetBin}.desktop
	)
	install(
		DIRECTORY "${CMAKE_SOURCE_DIR}/icons/hicolor"
		DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/icons"
	)
	install(
		FILES "${CMAKE_SOURCE_DIR}/icons/icon-512.svg"
		DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/apps"
		RENAME "ppsspp.svg"
	)
	install(
		FILES "${CMAKE_SOURCE_DIR}/assets/mime/ppsspp.xml"
		DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/mime/packages"
	)
endif()
