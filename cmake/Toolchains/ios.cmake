# Options:
#
# IOS_PLATFORM = OS (default)
#   This needs to be OS as the simulator cannot use the required GLES2.0 environment.
#   OS - the default, used to build for iPhone and iPad physical devices, which have an arm arch.
#
# CMAKE_IOS_SDK_ROOT = automatic(default) or /path/to/platform/Developer/SDKs/SDK folder
#   By default this location is automatcially chosen based on the CMAKE_IOS_DEVELOPER_ROOT value.
#   In this case it will always be the most up-to-date SDK found in the CMAKE_IOS_DEVELOPER_ROOT path.
#   If set manually, this will force the use of a specific SDK version

# PPSSPP platform flags
set(MOBILE_DEVICE ON)
set(USING_GLES2 ON)
set(IPHONEOS_DEPLOYMENT_TARGET 6.0)
add_definitions(
  -DGL_ETC1_RGB8_OES=0
  -U__STRICT_ANSI__
)

set(OPENGL_LIBRARIES "-framework OpenGLES")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mios-version-min=${IPHONEOS_DEPLOYMENT_TARGET}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mios-version-min=${IPHONEOS_DEPLOYMENT_TARGET}")
set(CMAKE_XCODE_ATTRIBUTE_CLANG_CXX_LIBRARY "libc++")

# Standard settings
set(CMAKE_SYSTEM_NAME Darwin)
set(CMAKE_SYSTEM_VERSION 1)
set(CMAKE_SYSTEM_PROCESSOR armv7)
set(IOS ON)
set(CMAKE_CROSSCOMPILING ON)
set(CMAKE_MACOSX_BUNDLE YES)
set(CMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED "NO")
set(CMAKE_XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET ${IPHONEOS_DEPLOYMENT_TARGET})

# Setup iOS platform unless specified manually with IOS_PLATFORM
if(NOT DEFINED IOS_PLATFORM)
  set(IOS_PLATFORM "OS")
endif()
set(IOS_PLATFORM ${IOS_PLATFORM} CACHE STRING "Type of iOS Platform")

# Check the platform selection and setup for developer root
if(IOS_PLATFORM STREQUAL "OS")
  set(IOS_SDK_NAME "iphoneos")
elseif(IOS_PLATFORM STREQUAL "SIMULATOR")
  set(IOS_SDK_NAME "iphonesimulator")
else()
  message (FATAL_ERROR "Unsupported IOS_PLATFORM value selected '${IOS_PLATFORM}'. Please choose OS or leave default")
endif()

# Setup iOS developer location unless specified manually with CMAKE_IOS_DEVELOPER_ROOT
if(NOT CMAKE_IOS_SDK_ROOT)
  execute_process(COMMAND xcrun --sdk ${IOS_SDK_NAME} --show-sdk-path
    OUTPUT_VARIABLE CMAKE_IOS_SDK_ROOT
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
  message (STATUS "Toolchain using default iOS SDK: ${CMAKE_IOS_SDK_ROOT}")
endif()

set(CMAKE_IOS_SDK_ROOT ${CMAKE_IOS_SDK_ROOT} CACHE PATH "Location of the selected iOS SDK")

# Set the sysroot default to the most recent SDK
set(CMAKE_OSX_SYSROOT ${CMAKE_IOS_SDK_ROOT} CACHE PATH "Sysroot used for iOS support")

# set the architecture for iOS 
if(IOS_PLATFORM STREQUAL "OS")
  # When ffmpeg has been rebuilt for arm64 use:
  set(IOS_ARCH "armv7;arm64")
  #set(IOS_ARCH "armv7")
else()
  set(IOS_ARCH "i386;x86_64")
endif()

set(CMAKE_OSX_ARCHITECTURES "${IOS_ARCH}" CACHE string  "Build architecture for iOS")
set(CMAKE_ASM_FLAGS "" CACHE STRING "" FORCE)
foreach(arch ${IOS_ARCH})
  set(CMAKE_ASM_FLAGS "${CMAKE_ASM_FLAGS} -arch ${arch}" CACHE STRING "" FORCE)
endforeach()

# Set the find root to the iOS developer roots and to user defined paths
set(CMAKE_FIND_ROOT_PATH ${CMAKE_IOS_SDK_ROOT} ${CMAKE_PREFIX_PATH} CACHE string  "iOS find search path root")

# default to searching for frameworks first
set(CMAKE_FIND_FRAMEWORK FIRST)

# set up the default search directories for frameworks
set(CMAKE_SYSTEM_FRAMEWORK_PATH
  ${CMAKE_IOS_SDK_ROOT}/System/Library/Frameworks
  ${CMAKE_IOS_SDK_ROOT}/System/Library/PrivateFrameworks
  ${CMAKE_IOS_SDK_ROOT}/Developer/Library/Frameworks
)

# only search the iOS sdks, not the remainder of the host filesystem
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

