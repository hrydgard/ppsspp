file(READ "/sys/firmware/devicetree/base/compatible" PPSSPP_PI_MODEL)

if(PPSSPP_PI_MODEL MATCHES "raspberrypi,4")
  set(PPSSPP_PI_MODEL4 ON)
endif()

if(NOT PPSSPP_PI_MODEL4 AND NOT EXISTS "/opt/vc/include/bcm_host.h")
  message(FATAL_ERROR "RaspberryPI platform not recognized")
endif()

include_directories(SYSTEM
  /opt/vc/include
  /opt/vc/include/interface/vcos/pthreads
  /opt/vc/include/interface/vmcx_host/linux
)

if(NOT PPSSPP_PI_MODEL4)
  add_definitions(
    -DPPSSPP_PLATFORM_RPI=1
    -U__GCC_HAVE_SYNC_COMPARE_AND_SWAP_2
  )
endif()

if(PPSSPP_PI_MODEL4)
  set(ARCH_FLAGS "-march=armv8-a+crc -mtune=cortex-a72 -funsafe-math-optimizations")
else()
  set(ARCH_FLAGS "-mcpu=cortex-a7 -mfpu=neon -mfloat-abi=hard")
endif()
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${ARCH_FLAGS}"  CACHE STRING "" FORCE)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${ARCH_FLAGS}" CACHE STRING "" FORCE)
set(CMAKE_ASM_FLAGS "${CMAKE_ASM_FLAGS} ${ARCH_FLAGS}" CACHE STRING "" FORCE)

set(CMAKE_EXE_LINKER_FLAGS "-L/opt/vc/lib" CACHE STRING "" FORCE)

if(NOT PPSSPP_PI_MODEL4)
  set(OPENGL_LIBRARIES brcmGLESv2 bcm_host)
  set(EGL_LIBRARIES brcmEGL)
  set(USING_FBDEV ON)
endif()
set(USING_GLES2 ON)
set(USE_WAYLAND_WSI OFF)
