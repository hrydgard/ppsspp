if(NOT EXISTS "/opt/vc/include/bcm_host.h")
  message(FATAL_ERROR "RaspberryPI platform not recognized")
endif()

include_directories(SYSTEM
  /opt/vc/include
  /opt/vc/include/interface/vcos/pthreads
  /opt/vc/include/interface/vmcx_host/linux
)

link_directories(
  /opt/vc/lib
)

add_definitions(
  -DRPI
)

set(ARCH_FLAGS "-mfpu=vfp -march=armv6j -mfloat-abi=hard")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${ARCH_FLAGS}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${ARCH_FLAGS}")
set(CMAKE_ASM_FLAGS "${CMAKE_ASM_FLAGS} ${ARCH_FLAGS}")

set(OPENGL_LIBRARIES GLESv2 bcm_host)
set(USING_GLES2 ON)
set(USING_FBDEV ON)

