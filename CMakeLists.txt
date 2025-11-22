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
				add_compile_options(-mfloat-abi=hard -marm -mtune=cortex-a15.cortex-a7 -mcpu=cortex-a15 -fomit-frame-pointer)
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
if(MINGW AND NOT USE_SYSTEM_FFMPEG)
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

# Default to bundled SDL2 on macOS, system SDL2 elsewhere.
if(APPLE AND NOT IOS)
	set(DEFAULT_USE_SYSTEM_LIBSDL2 OFF)
else()
	set(DEFAULT_USE_SYSTEM_LIBSDL2 ON)
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
option(USING_QT_UI "Set to ON if you wish to use the Qt frontend wrapper" ${USING_QT_UI})
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
option(USE_SYSTEM_LIBZIP "Dynamically link against system libzip" ${USE_SYSTEM_LIBZIP})
option(USE_SYSTEM_LIBSDL2 "Dynamically link against system SDL2" ${DEFAULT_USE_SYSTEM_LIBSDL2})
option(USE_SYSTEM_LIBPNG "Dynamically link against system libpng" ON)
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
endif()

if(ANDROID)
	set(MOBILE_DEVICE ON)
	set(USING_GLES2 ON)
endif()

if(ANDROID AND NOT LIBRETRO)
	set(CoreLibName ppsspp_jni)
	set(CoreLinkType SHARED)
else()
	set(CoreLibName Core)
	set(CoreLinkType STATIC)
endif()

if(NOT ANDROID AND NOT WIN32 AND (NOT APPLE OR IOS))
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
	set(OPENGL_LIBRARIES ${OPENGL_LIBRARIES} ${EGL_LIBRARIES})
endif()

if(NOT LIBRETRO AND NOT IOS AND NOT MACOSX)
	find_package(SDL2)
	find_package(SDL2_ttf)
	find_package(Fontconfig)

	# TODO: this can be removed once CI supports newer SDL2_ttf
	if (NOT SDL2_ttf_FOUND)
		find_package(PkgConfig)
		if(PkgConfig_FOUND)
			pkg_check_modules(SDL2_ttf_PKGCONFIG IMPORTED_TARGET SDL2_ttf)
		endif()
	endif()
endif()

if(MACOSX AND NOT IOS)
	if(USE_SYSTEM_LIBSDL2)
		find_package(SDL2)
		find_package(SDL2_ttf)
	else()
		find_library(SDL2Fwk SDL2 REQUIRED PATHS SDL/macOS)
		message(STATUS "found SDL2Fwk=${SDL2Fwk}")
		add_compile_definitions(HAVE_SYSCTLBYNAME)
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

	add_compile_options(-Wall -Werror=return-type -Wno-unused-function -Wno-sign-compare -Wno-unused-but-set-variable "$<$<COMPILE_LANGUAGE:CXX>:-Wno-reorder>" -Wno-unknown-pragmas -Wno-unused-value -Wno-unused-variable)
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

	if(X86 OR X86_64)
		# enable sse2 code generation
		if(NOT MACOSX)
			add_compile_options(-msse2)
		endif()
		if(NOT X86_64 AND NOT CLANG)
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
	add_compile_definitions(USING_WIN_UI)
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

set(CommonJIT
	Core/MIPS/JitCommon/JitCommon.cpp
	Core/MIPS/JitCommon/JitCommon.h
	Core/MIPS/JitCommon/JitBlockCache.cpp
	Core/MIPS/JitCommon/JitBlockCache.h
	Core/MIPS/JitCommon/JitState.cpp
	Core/MIPS/JitCommon/JitState.h
)

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
	Common/x64Emitter.h
)
source_group(x86 FILES ${CommonX86})

set(CommonARM
	Common/ArmCPUDetect.cpp
	Common/ArmEmitter.h
	Common/ArmEmitter.cpp
)
source_group(ARM FILES ${CommonARM})

set(CommonARM64
	Common/Arm64Emitter.h
	Common/Arm64Emitter.cpp
	Common/ArmEmitter.h
	Common/ArmEmitter.cpp
	Core/Util/DisArm64.h
	Core/Util/DisArm64.cpp
)
source_group(ARM64 FILES ${CommonARM64})

set(CommonMIPS
	Common/MipsCPUDetect.cpp
	Common/MipsEmitter.cpp
	Common/MipsEmitter.h
)
source_group(MIPS FILES ${CommonMIPS})

set(CommonRISCV64
	${CommonJIT}
	Common/RiscVCPUDetect.cpp
	Common/RiscVEmitter.cpp
	Common/RiscVEmitter.h
	Core/MIPS/fake/FakeJit.cpp
	Core/MIPS/fake/FakeJit.h
)
source_group(RISCV64 FILES ${CommonRISCV64})

set(CommonLOONGARCH64
	${CommonJIT}
	Common/LoongArchCPUDetect.cpp
	Common/LoongArch64Emitter.cpp
	Common/LoongArch64Emitter.h
	Core/MIPS/fake/FakeJit.cpp
	Core/MIPS/fake/FakeJit.h
)
source_group(LOONGARCH64 FILES ${CommonLOONGARCH64})

if(WIN32)
	set(CommonD3D
		Common/GPU/D3D11/thin3d_d3d11.cpp
		Common/GPU/D3D11/D3D11Loader.cpp
		Common/GPU/D3D11/D3D11Loader.h
	)
endif()

set(CommonVR
	Common/VR/OpenXRLoader.cpp
	Common/VR/OpenXRLoader.h
	Common/VR/PPSSPPVR.cpp
	Common/VR/PPSSPPVR.h
	Common/VR/VRBase.cpp
	Common/VR/VRBase.h
	Common/VR/VRFramebuffer.cpp
	Common/VR/VRFramebuffer.h
	Common/VR/VRInput.cpp
	Common/VR/VRInput.h
	Common/VR/VRMath.cpp
	Common/VR/VRMath.h
	Common/VR/VRRenderer.cpp
	Common/VR/VRRenderer.h
)
include_directories(ext/OpenXR-SDK/include)

# For ext/sol includes, that are usually included as sol/*.hpp
include_directories(ext)

add_library(Common STATIC
	${CommonX86}
	${CommonARM}
	${CommonARM64}
	${CommonMIPS}
	${CommonRISCV64}
	${CommonLOONGARCH64}
	${CommonD3D}
	${CommonVR}
	Common/Serialize/Serializer.cpp
	Common/Serialize/Serializer.h
	Common/Serialize/SerializeDeque.h
	Common/Serialize/SerializeFuncs.h
	Common/Serialize/SerializeList.h
	Common/Serialize/SerializeMap.h
	Common/Serialize/SerializeSet.h
	Common/Crypto/md5.cpp
	Common/Crypto/md5.h
	Common/Crypto/sha1.cpp
	Common/Crypto/sha1.h
	Common/Crypto/sha256.cpp
	Common/Crypto/sha256.h
	Common/Data/Collections/ConstMap.h
	Common/Data/Collections/FixedSizeQueue.h
	Common/Data/Collections/Hashmaps.h
	Common/Data/Collections/TinySet.h
	Common/Data/Collections/FastVec.h
	Common/Data/Collections/CharQueue.h
	Common/Data/Collections/CyclicBuffer.h
	Common/Data/Collections/ThreadSafeList.h
	Common/Data/Color/RGBAUtil.cpp
	Common/Data/Color/RGBAUtil.h
	Common/Data/Convert/ColorConv.cpp
	Common/Data/Convert/ColorConv.h
	Common/Data/Convert/SmallDataConvert.cpp
	Common/Data/Convert/SmallDataConvert.h
	Common/Data/Encoding/Base64.cpp
	Common/Data/Encoding/Base64.h
	Common/Data/Encoding/Compression.cpp
	Common/Data/Encoding/Compression.h
	Common/Data/Encoding/Shiftjis.h
	Common/Data/Encoding/Utf8.cpp
	Common/Data/Encoding/Utf8.h
	Common/Data/Encoding/Utf16.h
	Common/Data/Format/RIFF.cpp
	Common/Data/Format/RIFF.h
	Common/Data/Format/IniFile.cpp
	Common/Data/Format/IniFile.h
	Common/Data/Format/JSONReader.h
	Common/Data/Format/JSONReader.cpp
	Common/Data/Format/JSONWriter.h
	Common/Data/Format/JSONWriter.cpp
	Common/Data/Format/DDSLoad.cpp
	Common/Data/Format/DDSLoad.h
	Common/Data/Format/PNGLoad.cpp
	Common/Data/Format/PNGLoad.h
	Common/Data/Format/ZIMLoad.cpp
	Common/Data/Format/ZIMLoad.h
	Common/Data/Format/ZIMSave.cpp
	Common/Data/Format/ZIMSave.h
	Common/Data/Hash/Hash.cpp
	Common/Data/Hash/Hash.h
	Common/Data/Text/I18n.cpp
	Common/Data/Text/I18n.h
	Common/Data/Text/Parsers.cpp
	Common/Data/Text/Parsers.h
	Common/Data/Text/WrapText.cpp
	Common/Data/Text/WrapText.h
	Common/Data/Random/Rng.h
	Common/File/VFS/VFS.h
	Common/File/VFS/VFS.cpp
	Common/File/VFS/ZipFileReader.cpp
	Common/File/VFS/ZipFileReader.h
	Common/File/VFS/DirectoryReader.cpp
	Common/File/VFS/DirectoryReader.h
	Common/File/AndroidStorage.h
	Common/File/AndroidStorage.cpp
	Common/File/AndroidContentURI.h
	Common/File/AndroidContentURI.cpp
	Common/File/DiskFree.h
	Common/File/DiskFree.cpp
	Common/File/Path.h
	Common/File/Path.cpp
	Common/File/PathBrowser.h
	Common/File/PathBrowser.cpp
	Common/File/FileUtil.cpp
	Common/File/FileUtil.h
	Common/File/DirListing.cpp
	Common/File/DirListing.h
	Common/File/FileDescriptor.cpp
	Common/File/FileDescriptor.h
	Common/GPU/DataFormat.h
	Common/GPU/MiscTypes.h
	Common/GPU/GPUBackendCommon.cpp
	Common/GPU/GPUBackendCommon.h
	Common/GPU/thin3d.cpp
	Common/GPU/thin3d.h
	Common/GPU/thin3d_create.h
	Common/GPU/Shader.cpp
	Common/GPU/Shader.h
	Common/GPU/ShaderWriter.cpp
	Common/GPU/ShaderWriter.h
	Common/GPU/ShaderTranslation.h
	Common/GPU/ShaderTranslation.cpp
	Common/GPU/OpenGL/GLCommon.h
	Common/GPU/OpenGL/GLDebugLog.cpp
	Common/GPU/OpenGL/GLDebugLog.h
	Common/GPU/OpenGL/GLSLProgram.cpp
	Common/GPU/OpenGL/GLSLProgram.h
	Common/GPU/OpenGL/gl3stub.c
	Common/GPU/OpenGL/gl3stub.h
	Common/GPU/OpenGL/GLFeatures.cpp
	Common/GPU/OpenGL/GLFeatures.h
	Common/GPU/OpenGL/GLFrameData.cpp
	Common/GPU/OpenGL/GLFrameData.h
	Common/GPU/OpenGL/thin3d_gl.cpp
	Common/GPU/OpenGL/GLMemory.cpp
	Common/GPU/OpenGL/GLMemory.h
	Common/GPU/OpenGL/GLRenderManager.cpp
	Common/GPU/OpenGL/GLRenderManager.h
	Common/GPU/OpenGL/GLQueueRunner.cpp
	Common/GPU/OpenGL/GLQueueRunner.h
	Common/GPU/OpenGL/DataFormatGL.cpp
	Common/GPU/OpenGL/DataFormatGL.h
	Common/GPU/Vulkan/VulkanBarrier.cpp
	Common/GPU/Vulkan/VulkanBarrier.h
	Common/GPU/Vulkan/VulkanDebug.cpp
	Common/GPU/Vulkan/VulkanDebug.h
	Common/GPU/Vulkan/VulkanContext.cpp
	Common/GPU/Vulkan/VulkanContext.h
	Common/GPU/Vulkan/VulkanDescSet.cpp
	Common/GPU/Vulkan/VulkanDescSet.h
	Common/GPU/Vulkan/VulkanFramebuffer.cpp
	Common/GPU/Vulkan/VulkanFramebuffer.h
	Common/GPU/Vulkan/VulkanImage.cpp
	Common/GPU/Vulkan/VulkanImage.h
	Common/GPU/Vulkan/VulkanLoader.cpp
	Common/GPU/Vulkan/VulkanLoader.h
	Common/GPU/Vulkan/VulkanMemory.cpp
	Common/GPU/Vulkan/VulkanMemory.h
	Common/GPU/Vulkan/VulkanProfiler.cpp
	Common/GPU/Vulkan/VulkanProfiler.h
	Common/GPU/Vulkan/thin3d_vulkan.cpp
	Common/GPU/Vulkan/VulkanRenderManager.cpp
	Common/GPU/Vulkan/VulkanRenderManager.h
	Common/GPU/Vulkan/VulkanQueueRunner.cpp
	Common/GPU/Vulkan/VulkanQueueRunner.h
	Common/GPU/Vulkan/VulkanFrameData.cpp
	Common/GPU/Vulkan/VulkanFrameData.h
	Common/Input/GestureDetector.cpp
	Common/Input/GestureDetector.h
	Common/Input/KeyCodes.h
	Common/Input/InputState.cpp
	Common/Input/InputState.h
	Common/Math/fast/fast_matrix.c
	Common/Math/SIMDHeaders.h
	Common/Math/SIMDHeaders.h
	Common/Math/curves.cpp
	Common/Math/curves.h
	Common/Math/expression_parser.cpp
	Common/Math/expression_parser.h
	Common/Math/lin/matrix4x4.cpp
	Common/Math/lin/matrix4x4.h
	Common/Math/lin/vec3.cpp
	Common/Math/lin/vec3.h
	Common/Math/math_util.cpp
	Common/Math/math_util.h
	Common/Math/Statistics.h
	Common/Math/Statistics.cpp
	Common/Net/HTTPClient.cpp
	Common/Net/HTTPClient.h
	Common/Net/HTTPHeaders.cpp
	Common/Net/HTTPHeaders.h
	Common/Net/HTTPNaettRequest.cpp
	Common/Net/HTTPNaettRequest.h
	Common/Net/HTTPRequest.cpp
	Common/Net/HTTPRequest.h
	Common/Net/HTTPServer.cpp
	Common/Net/HTTPServer.h
	Common/Net/NetBuffer.cpp
	Common/Net/NetBuffer.h
	Common/Net/Resolve.cpp
	Common/Net/Resolve.h
	Common/Net/Sinks.cpp
	Common/Net/Sinks.h
	Common/Net/SocketCompat.h
	Common/Net/URL.cpp
	Common/Net/URL.h
	Common/Net/WebsocketServer.cpp
	Common/Net/WebsocketServer.h
	Common/Profiler/Profiler.cpp
	Common/Profiler/Profiler.h
	Common/Render/AtlasGen.cpp
	Common/Render/AtlasGen.h
	Common/Render/TextureAtlas.cpp
	Common/Render/TextureAtlas.h
	Common/Render/DrawBuffer.cpp
	Common/Render/DrawBuffer.h
	Common/Render/ManagedTexture.cpp
	Common/Render/ManagedTexture.h
	Common/Render/Text/draw_text.cpp
	Common/Render/Text/draw_text.h
	Common/Render/Text/draw_text_android.cpp
	Common/Render/Text/draw_text_android.h
	Common/Render/Text/draw_text_sdl.cpp
	Common/Render/Text/draw_text_sdl.h
	Common/Render/Text/draw_text_win.cpp
	Common/Render/Text/draw_text_win.h
	Common/Render/Text/draw_text_uwp.cpp
	Common/Render/Text/draw_text_uwp.h
	Common/Audio/AudioBackend.h
	Common/System/Display.cpp
	Common/System/Display.h
	Common/System/System.h
	Common/System/NativeApp.h
	Common/System/Request.cpp
	Common/System/Request.h
	Common/System/OSD.cpp
	Common/System/OSD.h
	Common/Thread/Channel.h
	Common/Thread/ParallelLoop.cpp
	Common/Thread/ParallelLoop.h
	Common/Thread/Promise.h
	Common/Thread/ThreadUtil.cpp
	Common/Thread/ThreadUtil.h
	Common/Thread/ThreadManager.cpp
	Common/Thread/ThreadManager.h
	Common/UI/AsyncImageFileView.cpp
	Common/UI/AsyncImageFileView.h
	Common/UI/Root.cpp
	Common/UI/Root.h
	Common/UI/Screen.cpp
	Common/UI/Screen.h
	Common/UI/UI.cpp
	Common/UI/UI.h
	Common/UI/Context.cpp
	Common/UI/Context.h
	Common/UI/IconCache.cpp
	Common/UI/IconCache.h
	Common/UI/UIScreen.cpp
	Common/UI/UIScreen.h
	Common/UI/Tween.cpp
	Common/UI/Tween.h
	Common/UI/View.cpp
	Common/UI/View.h
	Common/UI/ViewGroup.cpp
	Common/UI/ViewGroup.h
	Common/UI/ScrollView.cpp
	Common/UI/ScrollView.h
	Common/UI/TabHolder.cpp
	Common/UI/TabHolder.h
	Common/UI/PopupScreens.cpp
	Common/UI/PopupScreens.h
	Common/BitScan.h
	Common/BitSet.h
	Common/Buffer.h
	Common/Buffer.cpp
	Common/CodeBlock.h
	Common/Common.h
	Common/CommonFuncs.h
	Common/CommonTypes.h
	Common/DbgNew.h
	Common/FakeEmitter.h
	Common/FakeCPUDetect.cpp
	Common/ExceptionHandlerSetup.cpp
	Common/ExceptionHandlerSetup.h
	Common/GhidraClient.h
	Common/GhidraClient.cpp
	Common/Log.h
	Common/Log.cpp
	Common/Log/ConsoleListener.cpp
	Common/Log/ConsoleListener.h
	Common/Log/LogManager.cpp
	Common/Log/LogManager.h
	Common/LogReporting.cpp
	Common/LogReporting.h
	Common/MemArenaAndroid.cpp
	Common/MemArenaDarwin.cpp
	Common/MemArenaPosix.cpp
	Common/MemArenaWin32.cpp
	Common/MemArenaHorizon.cpp
	Common/MemArena.h
	Common/MemoryUtil.cpp
	Common/MemoryUtilHorizon.cpp
	Common/MemoryUtil.h
	Common/OSVersion.cpp
	Common/OSVersion.h
	Common/StringUtils.cpp
	Common/StringUtils.h
	Common/SysError.h
	Common/SysError.cpp
	Common/TimeUtil.cpp
	Common/TimeUtil.h
)

include_directories(Common)
setup_target_project(Common Common)

if(IOS)
	target_compile_definitions(Common PUBLIC GLES_SILENCE_DEPRECATION)
endif()

target_link_libraries(Common Ext::Snappy cpu_features)

if(NOT LIBRETRO)
    target_link_libraries(Common imgui)
endif()

if(ARM64)
	if(ANDROID)
		target_link_libraries(Common adrenotools)
	endif()
endif()

if(USING_GLES2 OR (USING_EGL AND NOT USING_FBDEV))
	find_package(X11)
endif()

add_library(gason STATIC
				ext/gason/gason.cpp
				ext/gason/gason.h
)

add_library(vma STATIC
				ext/vma/vk_mem_alloc.cpp
				ext/vma/vk_mem_alloc.h
)

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

add_library(cityhash STATIC
	ext/cityhash/city.cpp
	ext/cityhash/city.h
	ext/cityhash/citycrc.h
)
target_include_directories(cityhash PRIVATE ext/cityhash)

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
	target_link_libraries(libzip)
	include_directories(ext/libzip)
	set(LIBZIP_LIBRARY libzip)
endif()

# Arm platforms require at least libpng17.
if(ANDROID OR ARMV7 OR ARM64 OR ARM OR IOS)
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

add_library(basis_universal STATIC
	ext/basis_universal/basisu.h
	ext/basis_universal/basisu_containers.h
	ext/basis_universal/basisu_containers_impl.h
	ext/basis_universal/basisu_file_headers.h
	ext/basis_universal/basisu_transcoder.cpp
	ext/basis_universal/basisu_transcoder.h
	ext/basis_universal/basisu_transcoder_internal.h
	ext/basis_universal/basisu_transcoder_tables_astc.inc
	ext/basis_universal/basisu_transcoder_tables_astc_0_255.inc
	ext/basis_universal/basisu_transcoder_tables_atc_55.inc
	ext/basis_universal/basisu_transcoder_tables_atc_56.inc
	ext/basis_universal/basisu_transcoder_tables_bc7_m5_alpha.inc
	ext/basis_universal/basisu_transcoder_tables_bc7_m5_color.inc
	ext/basis_universal/basisu_transcoder_tables_dxt1_5.inc
	ext/basis_universal/basisu_transcoder_tables_dxt1_6.inc
	ext/basis_universal/basisu_transcoder_tables_pvrtc2_45.inc
	ext/basis_universal/basisu_transcoder_tables_pvrtc2_alpha_33.inc
	ext/basis_universal/basisu_transcoder_uastc.h
)
set(BASISU_LIBRARIES basis_universal)

set(nativeExtra)
set(nativeExtraLibs)

if(OPENXR AND NOT ARMV7_DEVICE)
	set(nativeExtraLibs ${nativeExtraLibs} openxr_loader)
endif()

if(IOS OR MACOSX)
	set(nativeExtra ${nativeExtra}
		Common/Render/Text/draw_text_cocoa.mm
		Common/Render/Text/draw_text_cocoa.h)
endif()

if(ANDROID)
	set(NativeAppSource ${NativeAppSource}
		android/jni/app-android.cpp
		android/jni/AndroidJavaGLContext.cpp
		android/jni/AndroidJavaGLContext.h
		android/jni/AndroidVulkanContext.cpp
		android/jni/AndroidVulkanContext.h
		android/jni/AndroidGraphicsContext.h
		android/jni/AndroidAudio.cpp
		android/jni/AndroidAudio.h
		android/jni/OpenSLContext.cpp
		android/jni/OpenSLContext.h
	)
	# No target
elseif(IOS AND NOT LIBRETRO)
	set(nativeExtra ${nativeExtra}
		ios/main.mm
		ios/AppDelegate.mm
		ios/AppDelegate.h
		ios/SceneDelegate.mm
		ios/SceneDelegate.h
		ios/DisplayManager.h
		ios/DisplayManager.mm
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
		ios/iOSVulkanContext.h
		ios/iOSVulkanContext.mm
		#ios/iOSOpenGLContext.h
		#ios/iOSOpenGLContext.cpp
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
		UI/DarwinFileSystemServices.mm
		UI/DarwinFileSystemServices.h
		Common/Battery/AppleBatteryClient.m
	)

	set(nativeExtraLibs ${nativeExtraLibs} "-framework Foundation -framework MediaPlayer -framework AudioToolbox -framework CoreGraphics -framework CoreMotion -framework QuartzCore -framework UIKit -framework GLKit -framework OpenAL -framework AVFoundation -framework CoreLocation -framework CoreText -framework CoreVideo -framework CoreMedia -framework CoreServices -framework Metal -framework IOSurface" )
	if(EXISTS "${CMAKE_IOS_SDK_ROOT}/System/Library/Frameworks/GameController.framework")
		set(nativeExtraLibs ${nativeExtraLibs} "-weak_framework GameController")
	endif()

	if(NOT ICONV_LIBRARY)
		set(nativeExtraLibs ${nativeExtraLibs} iconv)
	endif()

	# TODO: Enable arc globally?
	set_source_files_properties(ios/AppDelegate.mm PROPERTIES COMPILE_FLAGS -fobjc-arc)
	set_source_files_properties(ios/ViewController.mm PROPERTIES COMPILE_FLAGS -fobjc-arc)
	set_source_files_properties(ios/ViewControllerMetal.mm PROPERTIES COMPILE_FLAGS -fobjc-arc)
	set_source_files_properties(ios/iOSCoreAudio.mm PROPERTIES COMPILE_FLAGS -fobjc-arc)
	set_source_files_properties(ios/PPSSPPUIApplication.mm PROPERTIES COMPILE_FLAGS -fobjc-arc)
	set_source_files_properties(ios/iCade/iCadeReaderView.m PROPERTIES COMPILE_FLAGS -fobjc-arc)
	set_source_files_properties(ios/main.mm PROPERTIES COMPILE_FLAGS -fobjc-arc)
	set_source_files_properties(ios/CameraHelper.mm PROPERTIES COMPILE_FLAGS -fobjc-arc)
	set_source_files_properties(ios/AudioEngine.mm PROPERTIES COMPILE_FLAGS -fobjc-arc)
	set_source_files_properties(ios/LocationHelper.mm PROPERTIES COMPILE_FLAGS -fobjc-arc)
	set_source_files_properties(ios/DisplayManager.mm PROPERTIES COMPILE_FLAGS -fobjc-arc)
	set_source_files_properties(ios/Controls.mm PROPERTIES COMPILE_FLAGS -fobjc-arc)
	set_source_files_properties(UI/DarwinFileSystemServices.mm PROPERTIES COMPILE_FLAGS -fobjc-arc)
	set_source_files_properties(Common/Battery/AppleBatteryClient.m PROPERTIES COMPILE_FLAGS -fobjc-arc)
	set_source_files_properties(Common/Render/Text/draw_text_cocoa.mm PROPERTIES COMPILE_FLAGS -fobjc-arc)

	set(TargetBin PPSSPP)
elseif(IOS AND LIBRETRO)
	set(nativeExtraLibs ${nativeExtraLibs} "-framework GLKit")
elseif(USING_QT_UI)
	set(CMAKE_AUTOMOC ON)
	find_package(Qt5 COMPONENTS OpenGL Gui Core Multimedia)
	list(APPEND NativeAppSource
		Qt/QtMain.cpp
		Qt/QtMain.h
		Qt/mainwindow.cpp
		Qt/mainwindow.h
	)
	add_compile_definitions(USING_QT_UI)
	if(USING_GLES2)
		add_compile_definitions(QT_OPENGL_ES QT_OPENGL_ES_2)
	endif()
	if(APPLE)
		list(APPEND NativeAppSource
			UI/DarwinFileSystemServices.mm
			UI/DarwinFileSystemServices.h
			Common/Battery/AppleBatteryClient.m)
		set_source_files_properties(Common/Battery/AppleBatteryClient.m PROPERTIES COMPILE_FLAGS -fobjc-arc)
		set_source_files_properties(UI/DarwinFileSystemServices.mm PROPERTIES COMPILE_FLAGS -fobjc-arc)
		set_source_files_properties(Common/Render/Text/draw_text_cocoa.mm PROPERTIES COMPILE_FLAGS -fobjc-arc)
		set(nativeExtraLibs ${nativeExtraLibs} ${COCOA_LIBRARY} ${QUARTZ_CORE_LIBRARY} ${IOKIT_LIBRARY})
	endif()
	include_directories(Qt)
	include_directories(${CMAKE_CURRENT_BINARY_DIR})
	set(nativeExtraLibs ${nativeExtraLibs} Qt5::OpenGL Qt5::Gui Qt5::Core Qt5::Multimedia)
	set(TargetBin PPSSPPQt)

	# Enable SDL joystick if SDL is found
	if(SDL2_FOUND)
		add_compile_definitions(SDL)
		set(nativeExtra ${nativeExtra}
			SDL/SDLJoystick.h
			SDL/SDLJoystick.cpp
		)
		set(nativeExtraLibs ${nativeExtraLibs} SDL2::SDL2)
	endif()

elseif(WIN32)
	# Don't care about SDL.
	set(TargetBin PPSSPPWindows)
elseif(LIBRETRO)
else()
	if(GOLD)
		set(TargetBin PPSSPPGold)
	else()
		set(TargetBin PPSSPPSDL)
	endif()
	# Require SDL
	add_compile_definitions(SDL)
	set(nativeExtra ${nativeExtra}
		SDL/SDLJoystick.h
		SDL/SDLJoystick.cpp
		SDL/SDLMain.cpp
		SDL/SDLGLGraphicsContext.cpp
	)
	if(NOT USE_LIBNX)
		set(nativeExtra ${nativeExtra}
			SDL/SDLVulkanGraphicsContext.cpp
		)
	endif()
	if(SDL2_ttf_FOUND OR
		(SDL2_ttf_PKGCONFIG_FOUND AND
			SDL2_ttf_PKGCONFIG_VERSION VERSION_GREATER_EQUAL "2.0.18"))
		add_compile_definitions(USE_SDL2_TTF)
		if(FONTCONFIG_FOUND)
			add_compile_definitions(USE_SDL2_TTF_FONTCONFIG)
			set(nativeExtraLibs ${nativeExtraLibs} Fontconfig::Fontconfig)
		endif()
	elseif(SDL2_ttf_PKGCONFIG_FOUND)
		message(WARNING "Found SDL2_ttf <2.0.18 - this is too old, falling back to atlas")
	endif()
	if(SDL2_ttf_FOUND)
		set(nativeExtraLibs ${nativeExtraLibs} SDL2_ttf::SDL2_ttf)
	elseif(SDL2_ttf_PKGCONFIG_FOUND)
		set(nativeExtraLibs ${nativeExtraLibs} PkgConfig::SDL2_ttf_PKGCONFIG)
	endif()
	if(APPLE)
		set(nativeExtra ${nativeExtra}
		SDL/SDLMain.h
		SDL/SDLMain.mm
		SDL/SDLCocoaMetalLayer.h
		SDL/SDLCocoaMetalLayer.mm
		SDL/CocoaBarItems.mm
		SDL/CocoaBarItems.h
		SDL/PPSSPPAboutViewController.m
		SDL/PPSSPPAboutViewController.h
		SDL/MacCameraHelper.mm
		UI/DarwinFileSystemServices.mm
		UI/DarwinFileSystemServices.h
		Common/Battery/AppleBatteryClient.m
		UI/PSPNSApplicationDelegate.mm
		UI/PSPNSApplicationDelegate.h)

		# TODO
		# set_source_files_properties(SDL/SDLMain.mm PROPERTIES COMPILE_FLAGS -fobjc-arc)

		set_source_files_properties(UI/DarwinFileSystemServices.mm PROPERTIES COMPILE_FLAGS -fobjc-arc)
		set_source_files_properties(UI/PSPNSApplicationDelegate.mm PROPERTIES COMPILE_FLAGS -fobjc-arc)
		set_source_files_properties(SDL/CocoaMetalLayer.mm PROPERTIES COMPILE_FLAGS -fobjc-arc)
		set_source_files_properties(SDL/CocoaBarItems.mm PROPERTIES COMPILE_FLAGS -fobjc-arc)
		set_source_files_properties(SDL/PPSSPPAboutViewController.m PROPERTIES COMPILE_FLAGS -fobjc-arc)
		set_source_files_properties(SDL/MacCameraHelper.mm PROPERTIES COMPILE_FLAGS -fobjc-arc)
		set_source_files_properties(Common/Battery/AppleBatteryClient.m PROPERTIES COMPILE_FLAGS -fobjc-arc)
		set_source_files_properties(Common/Render/Text/draw_text_cocoa.mm PROPERTIES COMPILE_FLAGS -fobjc-arc)
		set(nativeExtraLibs ${nativeExtraLibs} ${COCOA_LIBRARY} ${QUARTZ_CORE_LIBRARY} ${IOKIT_LIBRARY} "-framework AVFoundation" "-framework CoreMedia" "-framework CoreVideo")

		if(USE_SYSTEM_LIBSDL2)
			set(nativeExtraLibs ${nativeExtraLibs} SDL2::SDL2)
		else()
			set(nativeExtraLibs ${nativeExtraLibs} ${SDL2Fwk})
		endif()
	elseif(USING_EGL)
		set(nativeExtraLibs ${nativeExtraLibs} pthread SDL2::SDL2)
	else()
		set(nativeExtraLibs ${nativeExtraLibs} SDL2::SDL2)
	endif()
endif()

if(WIN32)
	target_link_libraries(Common winmm dsound dxguid Version)
endif()

if(NOT LIBRETRO)
	list(APPEND NativeAppSource
		UI/AudioCommon.h
		UI/AudioCommon.cpp
	)
endif()

list(APPEND NativeAppSource
	UI/ImDebugger/ImDebugger.cpp
	UI/ImDebugger/ImDebugger.h
	UI/ImDebugger/ImGe.cpp
	UI/ImDebugger/ImGe.h
	UI/ImDebugger/ImConsole.cpp
	UI/ImDebugger/ImConsole.h
	UI/ImDebugger/ImDisasmView.cpp
	UI/ImDebugger/ImDisasmView.h
	UI/ImDebugger/ImMemView.cpp
	UI/ImDebugger/ImMemView.h
	UI/ImDebugger/ImStructViewer.cpp
	UI/ImDebugger/ImStructViewer.h
	UI/ImDebugger/ImJitViewer.cpp
	UI/ImDebugger/ImJitViewer.h
	UI/DiscordIntegration.cpp
	UI/NativeApp.cpp
	UI/BackgroundAudio.h
	UI/BackgroundAudio.cpp
	UI/Background.h
	UI/Background.cpp
	UI/ChatScreen.h
	UI/ChatScreen.cpp
	UI/BaseScreens.h
	UI/BaseScreens.cpp
	UI/DebugOverlay.cpp
	UI/DebugOverlay.h
	UI/DevScreens.cpp
	UI/DevScreens.h
	UI/DisplayLayoutScreen.cpp
	UI/DisplayLayoutScreen.h
	UI/EmuScreen.h
	UI/EmuScreen.cpp
	UI/GameInfoCache.h
	UI/GameInfoCache.cpp
	UI/IAPScreen.cpp
	UI/IAPScreen.h
	UI/MainScreen.h
	UI/MainScreen.cpp
	UI/MiscScreens.h
	UI/MiscScreens.cpp
	UI/MiscViews.h
	UI/MiscViews.cpp
	UI/PauseScreen.h
	UI/PauseScreen.cpp
	UI/SimpleDialogScreen.h
	UI/SimpleDialogScreen.cpp
	UI/TabbedDialogScreen.h
	UI/TabbedDialogScreen.cpp
	UI/GameScreen.h
	UI/GameScreen.cpp
	UI/GameSettingsScreen.h
	UI/GameSettingsScreen.cpp
	UI/DeveloperToolsScreen.h
	UI/DeveloperToolsScreen.cpp
	UI/DriverManagerScreen.h
	UI/DriverManagerScreen.cpp
	UI/GPUDriverTestScreen.h
	UI/GPUDriverTestScreen.cpp
	UI/TiltAnalogSettingsScreen.h
	UI/TiltAnalogSettingsScreen.cpp
	UI/TouchControlLayoutScreen.h
	UI/TouchControlLayoutScreen.cpp
	UI/TouchControlVisibilityScreen.h
	UI/TouchControlVisibilityScreen.cpp
	UI/GamepadEmu.h
	UI/GamepadEmu.cpp
	UI/JoystickHistoryView.h
	UI/JoystickHistoryView.cpp
	UI/OnScreenDisplay.h
	UI/OnScreenDisplay.cpp
	UI/ControlMappingScreen.h
	UI/ControlMappingScreen.cpp
	UI/RemoteISOScreen.h
	UI/RemoteISOScreen.cpp
	UI/ReportScreen.h
	UI/ReportScreen.cpp
	UI/SavedataScreen.h
	UI/SavedataScreen.cpp
	UI/SystemInfoScreen.h
	UI/SystemInfoScreen.cpp
	UI/Store.h
	UI/Store.cpp
	UI/UploadScreen.h
	UI/UploadScreen.cpp
	UI/CwCheatScreen.h
	UI/CwCheatScreen.cpp
	UI/InstallZipScreen.h
	UI/InstallZipScreen.cpp
	UI/JitCompareScreen.h
	UI/JitCompareScreen.cpp
	UI/MemStickScreen.h
	UI/MemStickScreen.cpp
	UI/ProfilerDraw.h
	UI/ProfilerDraw.cpp
	UI/CustomButtonMappingScreen.h
	UI/CustomButtonMappingScreen.cpp
	UI/Theme.h
	UI/Theme.cpp
	UI/UIAtlas.h
	UI/UIAtlas.cpp
	UI/RetroAchievementScreens.cpp
	UI/RetroAchievementScreens.h
)

if(ANDROID)
	if(ARM)
		set(NativeAppSource ${NativeAppSource} android/jni/ArmEmitterTest.cpp)
	elseif(ARM64)
		set(NativeAppSource ${NativeAppSource} android/jni/Arm64EmitterTest.cpp)
	endif()

	if (NOT LIBRETRO)
		set(nativeExtra ${nativeExtra} ${NativeAppSource})
	endif()
endif()

if (IOS)
	set(nativeExtra ${nativeExtra} ${NativeAppSource})
endif()

add_library(native STATIC
	${nativeExtra}
	Common/Render/Text/draw_text_qt.cpp
	Common/Render/Text/draw_text_qt.h
	ext/jpge/jpgd.cpp
	ext/jpge/jpgd.h
	ext/jpge/jpge.cpp
	ext/jpge/jpge.h
)

if(LINUX AND NOT ANDROID)
	set(RT_LIB rt)
endif()

set(ATOMIC_LIB)
if(ANDROID OR (LINUX AND ARM_DEVICE) OR (LINUX AND RISCV64) OR (LINUX AND LOONGARCH64))
	set(ATOMIC_LIB atomic)
endif()

target_link_libraries(native ${LIBZIP_LIBRARY} ${PNG_LIBRARIES} ${BASISU_LIBRARIES} ${ZLIB_LIBRARY} vma gason udis86 ${RT_LIB} ${nativeExtraLibs} ${ATOMIC_LIB} Common)
if(TARGET Ext::GLEW)
	target_link_libraries(native Ext::GLEW)
endif()

if(ANDROID)
	target_link_libraries(native log EGL OpenSLES)
elseif(WIN32)
	target_link_libraries(native ws2_32 winmm)
elseif(${CMAKE_SYSTEM_NAME} MATCHES "^(DragonFly|FreeBSD|NetBSD)$")
	target_link_libraries(native execinfo)
endif()

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
	ext/libkirk/kirk_engine.h
	ext/libkirk/kirk_common.h
)
target_include_directories(kirk PRIVATE ext/libkirk)

add_library(sfmt19937 STATIC
	ext/sfmt19937/SFMT.c
	ext/sfmt19937/SFMT.h
	ext/sfmt19937/SFMT-common.h
	ext/sfmt19937/SFMT-params.h
	ext/sfmt19937/SFMT-params19937.h
)
target_compile_definitions(sfmt19937 PRIVATE SFMT_MEXP=19937)
target_include_directories(sfmt19937 PRIVATE ext/sfmt19937)

add_library(xbrz STATIC
	ext/xbrz/xbrz.cpp
	ext/xbrz/xbrz.h
)
target_include_directories(xbrz PRIVATE ext/xbrz)

add_library(xxhash STATIC
	ext/xxhash.c
	ext/xxhash.h
)
target_include_directories(xxhash PRIVATE ext/xxhash)

set(CoreExtra)
set(CoreExtraLibs)

set(CoreExtra ${CoreExtra}
	Core/MIPS/IR/IRAnalysis.cpp
	Core/MIPS/IR/IRAnalysis.h
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
	Core/MIPS/IR/IRNativeCommon.cpp
	Core/MIPS/IR/IRNativeCommon.h
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
	Core/MIPS/ARM64/Arm64IRAsm.cpp
	Core/MIPS/ARM64/Arm64IRCompALU.cpp
	Core/MIPS/ARM64/Arm64IRCompBranch.cpp
	Core/MIPS/ARM64/Arm64IRCompFPU.cpp
	Core/MIPS/ARM64/Arm64IRCompLoadStore.cpp
	Core/MIPS/ARM64/Arm64IRCompSystem.cpp
	Core/MIPS/ARM64/Arm64IRCompVec.cpp
	Core/MIPS/ARM64/Arm64IRJit.cpp
	Core/MIPS/ARM64/Arm64IRJit.h
	Core/MIPS/ARM64/Arm64IRRegCache.cpp
	Core/MIPS/ARM64/Arm64IRRegCache.h
	GPU/Common/VertexDecoderArm64.cpp
	Core/Util/DisArm64.cpp
)

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
	Core/MIPS/x86/X64IRAsm.cpp
	Core/MIPS/x86/X64IRCompALU.cpp
	Core/MIPS/x86/X64IRCompBranch.cpp
	Core/MIPS/x86/X64IRCompFPU.cpp
	Core/MIPS/x86/X64IRCompLoadStore.cpp
	Core/MIPS/x86/X64IRCompSystem.cpp
	Core/MIPS/x86/X64IRCompVec.cpp
	Core/MIPS/x86/X64IRJit.cpp
	Core/MIPS/x86/X64IRJit.h
	Core/MIPS/x86/X64IRRegCache.cpp
	Core/MIPS/x86/X64IRRegCache.h
	GPU/Common/VertexDecoderX86.cpp
	GPU/Software/DrawPixelX86.cpp
	GPU/Software/SamplerX86.cpp
)

list(APPEND CoreExtra
	Core/MIPS/MIPS/MipsJit.cpp
	Core/MIPS/MIPS/MipsJit.h
)

list(APPEND CoreExtra
	Core/MIPS/RiscV/RiscVAsm.cpp
	Core/MIPS/RiscV/RiscVCompALU.cpp
	Core/MIPS/RiscV/RiscVCompBranch.cpp
	Core/MIPS/RiscV/RiscVCompFPU.cpp
	Core/MIPS/RiscV/RiscVCompLoadStore.cpp
	Core/MIPS/RiscV/RiscVCompSystem.cpp
	Core/MIPS/RiscV/RiscVCompVec.cpp
	Core/MIPS/RiscV/RiscVJit.cpp
	Core/MIPS/RiscV/RiscVJit.h
	Core/MIPS/RiscV/RiscVRegCache.cpp
	Core/MIPS/RiscV/RiscVRegCache.h
	GPU/Common/VertexDecoderRiscV.cpp
)

list(APPEND CoreExtra
	Core/MIPS/LoongArch64/LoongArch64Asm.cpp
	Core/MIPS/LoongArch64/LoongArch64CompALU.cpp
	Core/MIPS/LoongArch64/LoongArch64CompBranch.cpp
	Core/MIPS/LoongArch64/LoongArch64CompFPU.cpp
	Core/MIPS/LoongArch64/LoongArch64CompLoadStore.cpp
	Core/MIPS/LoongArch64/LoongArch64CompSystem.cpp
	Core/MIPS/LoongArch64/LoongArch64CompVec.cpp
	Core/MIPS/LoongArch64/LoongArch64Jit.cpp
	Core/MIPS/LoongArch64/LoongArch64Jit.h
	Core/MIPS/LoongArch64/LoongArch64RegCache.cpp
	Core/MIPS/LoongArch64/LoongArch64RegCache.h
	GPU/Common/VertexDecoderLoongArch64.cpp
)

if(NOT MOBILE_DEVICE)
	set(CoreExtra ${CoreExtra}
		Core/AVIDump.cpp
		Core/AVIDump.h
		Core/WaveFile.cpp
		Core/WaveFile.h
	)
endif()

set(GPU_GLES
	GPU/GLES/StencilBufferGLES.cpp
	GPU/GLES/GPU_GLES.cpp
	GPU/GLES/GPU_GLES.h
	GPU/GLES/FragmentTestCacheGLES.cpp
	GPU/GLES/FragmentTestCacheGLES.h
	GPU/GLES/FramebufferManagerGLES.cpp
	GPU/GLES/FramebufferManagerGLES.h
	GPU/GLES/ShaderManagerGLES.cpp
	GPU/GLES/ShaderManagerGLES.h
	GPU/GLES/StateMappingGLES.cpp
	GPU/GLES/StateMappingGLES.h
	GPU/GLES/TextureCacheGLES.cpp
	GPU/GLES/TextureCacheGLES.h
	GPU/GLES/DrawEngineGLES.cpp
	GPU/GLES/DrawEngineGLES.h
)

set(GPU_VULKAN
	GPU/Vulkan/DebugVisVulkan.cpp
	GPU/Vulkan/DebugVisVulkan.h
	GPU/Vulkan/DrawEngineVulkan.cpp
	GPU/Vulkan/DrawEngineVulkan.h
	GPU/Vulkan/FramebufferManagerVulkan.cpp
	GPU/Vulkan/FramebufferManagerVulkan.h
	GPU/Vulkan/GPU_Vulkan.cpp
	GPU/Vulkan/GPU_Vulkan.h
	GPU/Vulkan/PipelineManagerVulkan.cpp
	GPU/Vulkan/PipelineManagerVulkan.h
	GPU/Vulkan/ShaderManagerVulkan.cpp
	GPU/Vulkan/ShaderManagerVulkan.h
	GPU/Vulkan/StateMappingVulkan.cpp
	GPU/Vulkan/StateMappingVulkan.h
	GPU/Vulkan/TextureCacheVulkan.cpp
	GPU/Vulkan/TextureCacheVulkan.h
	GPU/Vulkan/VulkanUtil.cpp
	GPU/Vulkan/VulkanUtil.h
)

set(GPU_D3D11
	GPU/D3D11/DrawEngineD3D11.cpp
	GPU/D3D11/DrawEngineD3D11.h
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
	GPU/D3D11/TextureCacheD3D11.cpp
	GPU/D3D11/TextureCacheD3D11.h
)

# We build Vulkan even on Apple to avoid annoying build differences.
set(GPU_IMPLS ${GPU_GLES} ${GPU_VULKAN})
if(WIN32)
	list(APPEND GPU_IMPLS ${GPU_D3D11})
endif()

set(GPU_SOURCES
	${GPU_IMPLS}
	${GPU_NEON}
	GPU/Common/Draw2D.cpp
	GPU/Common/Draw2D.h
	GPU/Common/DepthBufferCommon.cpp
	GPU/Common/DepthRaster.cpp
	GPU/Common/DepthRaster.h
	GPU/Common/TextureShaderCommon.cpp
	GPU/Common/TextureShaderCommon.h
	GPU/Common/DepalettizeShaderCommon.cpp
	GPU/Common/DepalettizeShaderCommon.h
	GPU/Common/FragmentShaderGenerator.cpp
	GPU/Common/FragmentShaderGenerator.h
	GPU/Common/VertexShaderGenerator.cpp
	GPU/Common/VertexShaderGenerator.h
	GPU/Common/GeometryShaderGenerator.cpp
	GPU/Common/GeometryShaderGenerator.h
	GPU/Common/FramebufferManagerCommon.cpp
	GPU/Common/FramebufferManagerCommon.h
	GPU/Common/GPUDebugInterface.cpp
	GPU/Common/GPUDebugInterface.h
	GPU/Common/GPUStateUtils.cpp
	GPU/Common/GPUStateUtils.h
	GPU/Common/DrawEngineCommon.cpp
	GPU/Common/DrawEngineCommon.h
	GPU/Common/PresentationCommon.cpp
	GPU/Common/PresentationCommon.h
	GPU/Common/ReinterpretFramebuffer.cpp
	GPU/Common/ReinterpretFramebuffer.h
	GPU/Common/ShaderId.cpp
	GPU/Common/ShaderId.h
	GPU/Common/ShaderUniforms.cpp
	GPU/Common/ShaderUniforms.h
	GPU/Common/ShaderCommon.cpp
	GPU/Common/ShaderCommon.h
	GPU/Common/SplineCommon.cpp
	GPU/Common/SplineCommon.h
	GPU/Common/StencilCommon.cpp
	GPU/Common/StencilCommon.h
	GPU/Common/SoftwareTransformCommon.cpp
	GPU/Common/SoftwareTransformCommon.h
	GPU/Common/VertexDecoderCommon.cpp
	GPU/Common/VertexDecoderCommon.h
	GPU/Common/VertexDecoderHandwritten.cpp
	GPU/Common/VertexDecoderHandwritten.h
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
	GPU/Common/TextureReplacer.cpp
	GPU/Common/TextureReplacer.h
	GPU/Common/ReplacedTexture.cpp
	GPU/Common/ReplacedTexture.h
	GPU/Debugger/Breakpoints.cpp
	GPU/Debugger/Breakpoints.h
	GPU/Debugger/Debugger.cpp
	GPU/Debugger/Debugger.h
	GPU/Debugger/GECommandTable.cpp
	GPU/Debugger/GECommandTable.h
	GPU/Debugger/Playback.cpp
	GPU/Debugger/Playback.h
	GPU/Debugger/Record.cpp
	GPU/Debugger/Record.h
	GPU/Debugger/RecordFormat.h
	GPU/Debugger/State.cpp
	GPU/Debugger/State.h
	GPU/Debugger/Stepping.cpp
	GPU/Debugger/Stepping.h
	GPU/ge_constants.h
	GPU/GeConstants.cpp
	GPU/GPUDefinitions.h
	GPU/GeDisasm.cpp
	GPU/GeDisasm.h
	GPU/GPU.cpp
	GPU/GPU.h
	GPU/GPUCommon.cpp
	GPU/GPUCommon.h
	GPU/GPUCommonHW.cpp
	GPU/GPUCommonHW.h
	GPU/GPUState.cpp
	GPU/GPUState.h
	GPU/Math3D.cpp
	GPU/Math3D.h
	GPU/Software/BinManager.cpp
	GPU/Software/BinManager.h
	GPU/Software/Clipper.cpp
	GPU/Software/Clipper.h
	GPU/Software/DrawPixel.cpp
	GPU/Software/DrawPixel.h
	GPU/Software/FuncId.cpp
	GPU/Software/FuncId.h
	GPU/Software/Lighting.cpp
	GPU/Software/Lighting.h
	GPU/Software/Rasterizer.cpp
	GPU/Software/Rasterizer.h
	GPU/Software/RasterizerRectangle.cpp
	GPU/Software/RasterizerRectangle.h
	GPU/Software/RasterizerRegCache.cpp
	GPU/Software/RasterizerRegCache.h
	GPU/Software/Sampler.cpp
	GPU/Software/Sampler.h
	GPU/Software/SoftGpu.cpp
	GPU/Software/SoftGpu.h
	GPU/Software/TransformUnit.cpp
	GPU/Software/TransformUnit.h
)

# 'ppsspp_jni' on ANDROID, 'Core' everywhere else
# SHARED on ANDROID, STATIC everywhere else
add_library(${CoreLibName} ${CoreLinkType}
	${CoreExtra}
	${CommonJIT}
	Core/Config.cpp
	Core/Config.h
	Core/ConfigSettings.cpp
	Core/ConfigSettings.h
	Core/ConfigValues.h
	Core/ControlMapper.cpp
	Core/ControlMapper.h
	Core/Core.cpp
	Core/Core.h
	Core/Compatibility.cpp
	Core/Compatibility.h
	Core/CoreParameter.h
	Core/CoreTiming.cpp
	Core/CoreTiming.h
	Core/CwCheat.cpp
	Core/CwCheat.h
	Core/FrameTiming.cpp
	Core/FrameTiming.h
	Core/HDRemaster.cpp
	Core/HDRemaster.h
	Core/Instance.cpp
	Core/Instance.h
	Core/KeyMap.cpp
	Core/KeyMap.h
	Core/KeyMapDefaults.cpp
	Core/KeyMapDefaults.h
	Core/LuaContext.cpp
	Core/LuaContext.h
	Core/RetroAchievements.h
	Core/RetroAchievements.cpp
	Core/TiltEventProcessor.h
	Core/TiltEventProcessor.cpp
	Core/WebServer.cpp
	Core/WebServer.h
	Core/Debugger/Breakpoints.cpp
	Core/Debugger/Breakpoints.h
	Core/Debugger/DebugInterface.h
	Core/Debugger/MemBlockInfo.cpp
	Core/Debugger/MemBlockInfo.h
	Core/Debugger/SymbolMap.cpp
	Core/Debugger/SymbolMap.h
	Core/Debugger/Watch.h
	Core/Debugger/DisassemblyManager.cpp
	Core/Debugger/DisassemblyManager.h
	Core/Debugger/WebSocket.cpp
	Core/Debugger/WebSocket.h
	Core/Debugger/WebSocket/BreakpointSubscriber.cpp
	Core/Debugger/WebSocket/BreakpointSubscriber.h
	Core/Debugger/WebSocket/CPUCoreSubscriber.cpp
	Core/Debugger/WebSocket/CPUCoreSubscriber.h
	Core/Debugger/WebSocket/DisasmSubscriber.cpp
	Core/Debugger/WebSocket/DisasmSubscriber.h
	Core/Debugger/WebSocket/GameBroadcaster.cpp
	Core/Debugger/WebSocket/GameBroadcaster.h
	Core/Debugger/WebSocket/GameSubscriber.cpp
	Core/Debugger/WebSocket/GameSubscriber.h
	Core/Debugger/WebSocket/ClientConfigSubscriber.cpp
	Core/Debugger/WebSocket/ClientConfigSubscriber.h
	Core/Debugger/WebSocket/GPUBufferSubscriber.cpp
	Core/Debugger/WebSocket/GPUBufferSubscriber.h
	Core/Debugger/WebSocket/GPURecordSubscriber.cpp
	Core/Debugger/WebSocket/GPURecordSubscriber.h
	Core/Debugger/WebSocket/GPUStatsSubscriber.cpp
	Core/Debugger/WebSocket/GPUStatsSubscriber.h
	Core/Debugger/WebSocket/HLESubscriber.cpp
	Core/Debugger/WebSocket/HLESubscriber.h
	Core/Debugger/WebSocket/InputBroadcaster.cpp
	Core/Debugger/WebSocket/InputBroadcaster.h
	Core/Debugger/WebSocket/InputSubscriber.cpp
	Core/Debugger/WebSocket/InputSubscriber.h
	Core/Debugger/WebSocket/LogBroadcaster.cpp
	Core/Debugger/WebSocket/LogBroadcaster.h
	Core/Debugger/WebSocket/MemoryInfoSubscriber.cpp
	Core/Debugger/WebSocket/MemoryInfoSubscriber.h
	Core/Debugger/WebSocket/MemorySubscriber.cpp
	Core/Debugger/WebSocket/MemorySubscriber.h
	Core/Debugger/WebSocket/ReplaySubscriber.cpp
	Core/Debugger/WebSocket/ReplaySubscriber.h
	Core/Debugger/WebSocket/SteppingBroadcaster.cpp
	Core/Debugger/WebSocket/SteppingBroadcaster.h
	Core/Debugger/WebSocket/SteppingSubscriber.cpp
	Core/Debugger/WebSocket/SteppingSubscriber.h
	Core/Debugger/WebSocket/WebSocketUtils.cpp
	Core/Debugger/WebSocket/WebSocketUtils.h
	Core/Dialog/PSPDialog.cpp
	Core/Dialog/PSPDialog.h
	Core/Dialog/PSPGamedataInstallDialog.cpp
	Core/Dialog/PSPGamedataInstallDialog.h
	Core/Dialog/PSPMsgDialog.cpp
	Core/Dialog/PSPMsgDialog.h
	Core/Dialog/PSPNetconfDialog.cpp
	Core/Dialog/PSPNetconfDialog.h
	Core/Dialog/PSPNpSigninDialog.cpp
	Core/Dialog/PSPNpSigninDialog.h
	Core/Dialog/PSPOskDialog.cpp
	Core/Dialog/PSPOskDialog.h
	Core/Dialog/PSPOskConstants.cpp
	Core/Dialog/PSPOskConstants.h
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
	Core/ELF/PSPElfTypes.h
	Core/ELF/PBPReader.cpp
	Core/ELF/PBPReader.h
	Core/ELF/PrxDecrypter.cpp
	Core/ELF/PrxDecrypter.h
	Core/ELF/ParamSFO.cpp
	Core/ELF/ParamSFO.h
	Core/FFMPEGCompat.h
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
	Core/HLE/PSPThreadContext.h
	Core/HLE/KUBridge.h
	Core/HLE/KUBridge.cpp
	Core/HLE/Plugins.h
	Core/HLE/Plugins.cpp
	Core/HLE/ThreadQueueList.h
	Core/HLE/__sceAudio.cpp
	Core/HLE/__sceAudio.h
	Core/HLE/sceAdler.cpp
	Core/HLE/sceAdler.h
	Core/HLE/AtracBase.h
	Core/HLE/AtracCtx.cpp
	Core/HLE/AtracCtx.h
	Core/HLE/AtracCtx2.cpp
	Core/HLE/AtracCtx2.h
	Core/HLE/NetInetConstants.cpp
	Core/HLE/NetInetConstants.h
	Core/HLE/SocketManager.cpp
	Core/HLE/SocketManager.h
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
	Core/HLE/sceKernelHeap.cpp
	Core/HLE/sceKernelHeap.h
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
	Core/HLE/sceNet_lib.cpp
	Core/HLE/sceNet_lib.h
	Core/HLE/NetAdhocCommon.cpp
	Core/HLE/NetAdhocCommon.h
	Core/HLE/sceNetAdhoc.cpp
	Core/HLE/sceNetAdhoc.h
	Core/HLE/sceNetAdhocMatching.cpp
	Core/HLE/sceNetAdhocMatching.h
	Core/HLE/sceNetInet.cpp
	Core/HLE/sceNetInet.h
	Core/HLE/sceNetApctl.cpp
	Core/HLE/sceNetApctl.h
	Core/HLE/sceNetResolver.cpp
	Core/HLE/sceNetResolver.h
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
	Core/HLE/sceAac.cpp
	Core/HLE/sceAac.h
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
	Core/HLE/sceReg.cpp
	Core/HLE/sceReg.h
	Core/HLE/sceRtc.cpp
	Core/HLE/sceRtc.h
	Core/HLE/sceSas.cpp
	Core/HLE/sceSas.h
	Core/HLE/sceSfmt19937.cpp
	Core/HLE/sceSfmt19937.h
	Core/HLE/sceSha256.cpp
	Core/HLE/sceSha256.h
	Core/HLE/sceSircs.cpp
	Core/HLE/sceSircs.h
	Core/HLE/sceSsl.cpp
	Core/HLE/sceSsl.h
	Core/HLE/sceUmd.cpp
	Core/HLE/sceUmd.h
	Core/HLE/sceUsb.cpp
	Core/HLE/sceUsb.h
	Core/HLE/sceUsbAcc.cpp
	Core/HLE/sceUsbAcc.h
	Core/HLE/sceUsbCam.cpp
	Core/HLE/sceUsbCam.h
	Core/HLE/sceUsbGps.cpp
	Core/HLE/sceUsbGps.h
	Core/HLE/sceUsbMic.cpp
	Core/HLE/sceUsbMic.h
	Core/HLE/sceUtility.cpp
	Core/HLE/sceUtility.h
	Core/HLE/sceVaudio.cpp
	Core/HLE/sceVaudio.h
	Core/HLE/scePspNpDrm_user.cpp
	Core/HLE/scePspNpDrm_user.h
	Core/HLE/sceNp.cpp
	Core/HLE/sceNp.h
	Core/HLE/sceNp2.cpp
	Core/HLE/sceNp2.h
	Core/HLE/scePauth.cpp
	Core/HLE/scePauth.h
	Core/HW/SimpleAudioDec.cpp
	Core/HW/SimpleAudioDec.h
	Core/HW/Atrac3Standalone.cpp
	Core/HW/Atrac3Standalone.h
	Core/HW/SimpleAudioDec.h
	Core/HW/AsyncIOManager.cpp
	Core/HW/AsyncIOManager.h
	Core/HW/BufferQueue.cpp
	Core/HW/BufferQueue.h
	Core/HW/Camera.cpp
	Core/HW/Camera.h
	Core/HW/Display.cpp
	Core/HW/Display.h
	Core/HW/GranularMixer.cpp
	Core/HW/GranularMixer.h
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
	Core/FileLoaders/ZipFileLoader.cpp
	Core/FileLoaders/ZipFileLoader.h
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
	Core/MIPS/MIPSVFPUFallbacks.cpp
	Core/MIPS/MIPSVFPUFallbacks.h
	Core/MIPS/MIPSAsm.cpp
	Core/MIPS/MIPSAsm.h
	Core/MIPS/MIPSTracer.cpp
	Core/MIPS/MIPSTracer.h
	Core/MemFault.cpp
	Core/MemFault.h
	Core/MemMap.cpp
	Core/MemMap.h
	Core/MemMapFunctions.cpp
	Core/MemMapHelpers.h
	Core/PSPLoaders.cpp
	Core/PSPLoaders.h
	Core/Reporting.cpp
	Core/Reporting.h
	Core/Replay.cpp
	Core/Replay.h
	Core/SaveState.cpp
	Core/SaveState.h
	Core/Screenshot.cpp
	Core/Screenshot.h
	Core/System.cpp
	Core/System.h
	Core/Util/AtracTrack.cpp
	Core/Util/AtracTrack.h
	Core/Util/AudioFormat.cpp
	Core/Util/AudioFormat.h
	Core/Util/GameManager.cpp
	Core/Util/GameManager.h
	Core/Util/MemStick.cpp
	Core/Util/MemStick.h
	Core/Util/GameDB.cpp
	Core/Util/GameDB.h
	Core/Util/PortManager.cpp
	Core/Util/PortManager.h
	Core/Util/BlockAllocator.cpp
	Core/Util/BlockAllocator.h
	Core/Util/PPGeDraw.cpp
	Core/Util/PPGeDraw.h
	Core/Util/RecentFiles.cpp
	Core/Util/RecentFiles.h
	${GPU_SOURCES}
	ext/disarm.cpp
	ext/disarm.h
	ext/riscv-disas.cpp
	ext/riscv-disas.h
	ext/loongarch-disasm.cpp
	ext/loongarch-disasm.h
	${CMAKE_CURRENT_BINARY_DIR}/git-version.cpp
)

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

set(GlslangLibs glslang OGLCompiler OSDependent SPIRV spirv-cross-glsl)

if (ENABLE_SPVREMAPPER)
	list(APPEND GlslangLibs SPVRemapper)
endif()

if(WIN32)
	set(GlslangLibs ${GlslangLibs} spirv-cross-hlsl)
endif()

if(OPENGL_opengl_LIBRARY AND OpenGL_GL_PREFERENCE STREQUAL GLVND AND NOT APPLE)
	set(OPENGL_LIBRARIES OpenGL::OpenGL)
endif()

if(USE_SYSTEM_ZSTD)
	find_package(ZSTD REQUIRED)
	target_include_directories(${CoreLibName} PRIVATE ${ZSTD_INCLUDE_DIR})
	target_link_libraries(${CoreLibName} ${ZSTD_LIBRARY})
else()
	add_subdirectory(ext/zstd-build)
	set(CoreExtraLibs ${CoreExtraLibs} libzstd_static)
	include_directories(ext/zstd/lib)
endif()

include_directories(ext/libchdr/include)

target_link_libraries(${CoreLibName} Common native chdr kirk cityhash sfmt19937 xbrz xxhash rcheevos minimp3 at3_standalone lua ${GlslangLibs}
	${CoreExtraLibs} ${OPENGL_LIBRARIES} ${X11_LIBRARIES} ${CMAKE_DL_LIBS})

# Winsock
if(WIN32)
	target_link_libraries(${CoreLibName} ws2_32)
endif()

if(NOT HTTPS_NOT_AVAILABLE)
	target_link_libraries(${CoreLibName} naett)
endif()

target_compile_features(${CoreLibName} PUBLIC cxx_std_17)

if(FFmpeg_FOUND)
	target_compile_definitions(${CoreLibName} PRIVATE USE_FFMPEG=1)
	if (HAVE_LIBAVCODEC_CONST_AVCODEC)
		target_compile_definitions(${CoreLibName} PRIVATE HAVE_LIBAVCODEC_CONST_AVCODEC=1)
	endif()
	set_target_properties(${CoreLibName} PROPERTIES NO_SYSTEM_FROM_IMPORTED true)
	target_include_directories(${CoreLibName} BEFORE PUBLIC ${FFmpeg_INCLUDE_avcodec} ${FFmpeg_INCLUDE_avformat})
	target_link_libraries(${CoreLibName}
		FFmpeg::avcodec
		FFmpeg::avformat
		FFmpeg::avutil
		FFmpeg::swresample
		FFmpeg::swscale
		${ZLIB_LIBRARY}
	)
endif()

# Discord integration
if(USE_DISCORD AND NOT IOS AND NOT LIBRETRO)
	add_compile_definitions(USE_DISCORD=1)
	target_link_libraries(${CoreLibName} discord-rpc)
endif()

# miniUPnPc integration (MiniUPnPc supposed to works on any POSIX system, not sure if some of these are redundant/not needed tho)
if(USE_MINIUPNPC)
	if(USE_SYSTEM_MINIUPNPC)
		find_package(MINIUPNPC REQUIRED)
		target_include_directories(${CoreLibName} PRIVATE ${MINIUPNP_INCLUDE_DIR})
		target_link_libraries(${CoreLibName} ${MINIUPNP_LIBRARY})
		add_compile_definitions(WITH_UPNP USE_SYSTEM_MINIUPNPC)
	else()
		set (MINIUPNPC_VERSION 2.2) # used by miniupnpcstrings.h.cmake
		set (MINIUPNPC_API_VERSION 18)
		option(UPNPC_BUILD_STATIC "Build static library" TRUE)
		option(NO_GETADDRINFO "Define NO_GETADDRINFO" FALSE)
		mark_as_advanced(NO_GETADDRINFO)
		if (NO_GETADDRINFO)
			add_compile_definitions(NO_GETADDRINFO)
		endif()

		if (NOT WIN32)
			add_compile_definitions (MINIUPNPC_SET_SOCKET_TIMEOUT)
			add_compile_definitions (_BSD_SOURCE _DEFAULT_SOURCE _POSIX_C_SOURCE=200112L)
		endif()
		if (MACOSX)
			add_compile_definitions (_DARWIN_C_SOURCE)
		endif()
		if(WIN32)
			add_compile_definitions(WIN32 MINIUPNP_EXPORTS)
		else()
			add_compile_options(-fPIC)
		endif()

		add_compile_definitions(WITH_UPNP MINIUPNP_STATICLIB)
		set(MINIUPNP_DIR "ext/miniupnp/miniupnpc")
		include_directories(${CMAKE_CURRENT_BINARY_DIR})
		include_directories(ext/miniupnp/miniupnpc/src)
		include_directories(ext/miniupnp/miniupnpc/include)
		configure_file(${MINIUPNP_DIR}/miniupnpcstrings.h.cmake ${CMAKE_CURRENT_BINARY_DIR}/miniupnpcstrings.h) # by default miniupnp repo doesn't contains miniupnpcstrings.h and need to be generated
		set(MINIUPNPC_SOURCES
				# the needed bits of miniupnpc (no python module, no tests, no cli)
			${MINIUPNP_DIR}/src/addr_is_reserved.c
			${MINIUPNP_DIR}/src/connecthostport.c
			${MINIUPNP_DIR}/src/igd_desc_parse.c
			${MINIUPNP_DIR}/src/minisoap.c
			${MINIUPNP_DIR}/src/minissdpc.c
			${MINIUPNP_DIR}/src/miniupnpc.c
				#${MINIUPNP_DIR}/miniupnpcmodule.c
			${MINIUPNP_DIR}/src/miniwget.c
			${MINIUPNP_DIR}/src/minixml.c
			${MINIUPNP_DIR}/src/portlistingparse.c
			${MINIUPNP_DIR}/src/receivedata.c
				#${MINIUPNP_DIR}/upnpc.c # causing an error due to already existing _main()
			${MINIUPNP_DIR}/src/upnpcommands.c
			${MINIUPNP_DIR}/src/upnpdev.c
			${MINIUPNP_DIR}/src/upnperrors.c
			${MINIUPNP_DIR}/src/upnpreplyparse.c
			${CMAKE_CURRENT_BINARY_DIR}/miniupnpcstrings.h
		)
		if (NOT WIN32 AND NOT CMAKE_SYSTEM_NAME STREQUAL "AmigaOS")
			#set(MINIUPNPC_SOURCES ${MINIUPNPC_SOURCES} minissdpc.c) # causing an error due to duplication in MINIUPNPC_SOURCES?
		endif()
		if (WIN32)
			set_source_files_properties(${MINIUPNPC_SOURCES} PROPERTIES  COMPILE_DEFINITIONS "MINIUPNP_STATICLIB;MINIUPNP_EXPORTS")
			set(LDLIBS ws2_32 iphlpapi ${LDLIBS})
			#elseif (CMAKE_SYSTEM_NAME STREQUAL "Solaris")
			#  find_library (SOCKET_LIBRARY NAMES socket)
			#  find_library (NSL_LIBRARY NAMES nsl)
			#  find_library (RESOLV_LIBRARY NAMES resolv)
			#  set (LDLIBS ${SOCKET_LIBRARY} ${NSL_LIBRARY} ${RESOLV_LIBRARY} ${LDLIBS})
		endif()
		if (UPNPC_BUILD_STATIC)
			add_library(miniupnpc STATIC ${MINIUPNPC_SOURCES})
			target_link_libraries(${CoreLibName} miniupnpc ${LDLIBS})
			set(UPNPC_LIBRARY miniupnpc)
			if (MSVC)
				# Suppress noise warnings
				target_compile_definitions(miniupnpc PRIVATE _CRT_SECURE_NO_WARNINGS _WINSOCK_DEPRECATED_NO_WARNINGS)
			endif()
			if (WIN32)
				target_link_libraries(miniupnpc ws2_32)
			endif()
		endif()
	endif()
endif()

setup_target_project(${CoreLibName} Core)

# Generate git-version at build time.
add_custom_target(GitVersion DEPENDS something_that_never_exists)

set(WIN_VERSION_CMD "")
if (WIN32)
	set(WIN_VERSION_CMD COMMAND ${CMAKE_SOURCE_DIR}/Windows/git-version-gen.cmd PPSSPPWindows)
endif()

add_custom_command(OUTPUT something_that_never_exists
	COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
		-DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
		-P ${CMAKE_CURRENT_SOURCE_DIR}/git-version.cmake
	${WIN_VERSION_CMD})

set_source_files_properties(${CMAKE_CURRENT_BINARY_DIR}/git-version.cpp
	PROPERTIES GENERATED TRUE
	SKIP_AUTOMOC ON)
add_dependencies(${CoreLibName} GitVersion)

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
	Windows/HidInputDevice.h
	Windows/HidInputDevice.cpp
	Windows/EmuThread.cpp
	Windows/EmuThread.h
	Windows/GPU/D3D11Context.cpp
	Windows/GPU/D3D11Context.h
	Windows/GPU/WindowsGLContext.cpp
	Windows/GPU/WindowsVulkanContext.cpp
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

if(ANDROID AND ARM64)
	# Support 16kb page size on Android
	target_link_options(${CoreLibName} PRIVATE "-Wl,-z,max-page-size=16384")
endif()

list(APPEND LinkCommon ${CoreLibName} ${CMAKE_THREAD_LIBS_INIT})

if(WIN32)
	list(APPEND LinkCommon kernel32 user32 gdi32 shell32 comctl32 dsound xinput winmm dinput8 ole32 winspool ksuser dwmapi mf uxtheme mfplat mfreadwrite mfuuid shlwapi setupapi hid)
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
	assets/7z.png
	assets/compat.ini
	assets/infra-dns.json
	assets/gamecontrollerdb.txt
	assets/langregion.ini
	assets/ppge_atlas.zim
	assets/ppge_atlas.meta
	assets/rargray.png
	assets/unknown.png
	assets/zip.png
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
		headless/HeadlessHost.cpp
		headless/HeadlessHost.h
		headless/Compare.cpp
		headless/Compare.h
		headless/SDLHeadlessHost.cpp
		headless/SDLHeadlessHost.h
	)
	if(WIN32)
		list(APPEND HeadlessSource
			headless/WindowsHeadlessHost.cpp
			headless/WindowsHeadlessHost.h
			Windows/GPU/D3D11Context.cpp
			Windows/GPU/D3D11Context.h
			Windows/GPU/WindowsGLContext.cpp
			Windows/GPU/WindowsVulkanContext.cpp
			Windows/W32Util/ShellUtil.cpp
			Windows/W32Util/ShellUtil.h
			Windows/CaptureDevice.cpp
			Windows/CaptureDevice.h
			Windows/W32Util/Misc.cpp
			Windows/W32Util/Misc.h
		)
	endif()
	add_executable(PPSSPPHeadless ${HeadlessSource})
	target_link_libraries(PPSSPPHeadless ${COCOA_LIBRARY} ${QUARTZ_CORE_LIBRARY} ${IOKIT_LIBRARY} ${LinkCommon})
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
		unittest/TestRiscVEmitter.cpp
		unittest/TestLoongArch64Emitter.cpp
		unittest/TestSoftwareGPUJit.cpp
		unittest/TestThreadManager.cpp
		unittest/JitHarness.cpp
		Core/MIPS/ARM/ArmRegCache.cpp
		Core/MIPS/ARM/ArmRegCacheFPU.cpp
	)
	if(WIN32)
		target_sources(PPSSPPUnitTest PRIVATE
			Windows/CaptureDevice.cpp
			Windows/CaptureDevice.h
		)
	endif()
	target_link_libraries(PPSSPPUnitTest ${COCOA_LIBRARY} ${QUARTZ_CORE_LIBRARY} ${IOKIT_LIBRARY} ${LinkCommon} Common)
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
	add_test(matrix_transpose PPSSPPUnitTest MatrixTranspose)
	add_test(parse_lbn PPSSPPUnitTest ParseLBN)
	add_test(quick_texhash PPSSPPUnitTest QuickTexHash)
	add_test(clz PPSSPPUnitTest CLZ)
	add_test(shadergen PPSSPPUnitTest ShaderGenerators)
endif()

if(ATLAS_TOOL)
	include_directories(ext/freetype/include)

	set(AtlasToolSource
		ext/native/tools/atlastool.cpp
	)
	add_executable(AtlasTool ${AtlasToolSource})
	target_link_libraries(AtlasTool ${LinkCommon} freetype)
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
			if(USING_QT_UI)
				add_custom_command(TARGET ${TargetBin} POST_BUILD COMMAND /bin/bash "${CMAKE_SOURCE_DIR}/Qt/macbundle.sh" "${CMAKE_BINARY_DIR}/PPSSPPQt.app")
			elseif(NOT USE_SYSTEM_LIBSDL2)
				add_custom_command(TARGET ${TargetBin} POST_BUILD COMMAND /bin/bash "${CMAKE_SOURCE_DIR}/SDL/macbundle.sh" "${CMAKE_BINARY_DIR}/${TargetBin}.app" "${TargetBin}")
			endif()
		endif()
	elseif(WIN32)
		add_executable(${TargetBin} WIN32 ${NativeAppSource})
		if(NOT MSVC)
			target_compile_options(${TargetBin} PRIVATE $<$<CXX_COMPILER_ID:Clang>:-Wno-c++11-narrowing>)
			target_link_libraries(${TargetBin} Version Dwmapi Avrt)
		endif()
		set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${TargetBin})
	else()
		add_executable(${TargetBin} ${NativeAppSource})
	endif()
	if(ANDROID AND ARM64)
		target_link_options(${TargetBin} PRIVATE "-Wl,-z,max-page-size=16384")
	endif()
	target_link_libraries(${TargetBin} ${LinkCommon} Common)
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

			#XCODE_ATTRIBUTE_CODE_SIGN_ENTITLEMENTS ${BUNDLE_ENTITLEMENTS}
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
