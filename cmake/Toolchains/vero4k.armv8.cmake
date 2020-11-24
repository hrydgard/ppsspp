include_directories(SYSTEM
  /opt/vero3/include
  /opt/vero3/include/EGL
  /opt/vero3/include/GLES2
)

set(ARCH_FLAGS "-march=armv8-a+crc -mtune=cortex-a53 -mfloat-abi=hard -funsafe-math-optimizations")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${ARCH_FLAGS}"  CACHE STRING "" FORCE)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${ARCH_FLAGS}" CACHE STRING "" FORCE)
set(CMAKE_ASM_FLAGS "${CMAKE_ASM_FLAGS} ${ARCH_FLAGS}" CACHE STRING "" FORCE)

set(CMAKE_EXE_LINKER_FLAGS "-L/opt/vero3/lib" CACHE STRING "" FORCE)

set(OPENGL_LIBRARIES /opt/vero3/lib/libGLESv2.so)
set(EGL_LIBRARIES /opt/vero3/lib/libEGL.so)

set(USING_GLES2 ON)
set(USING_EGL ON)
set(USING_FBDEV ON)
set(FORCED_CPU armv7)
set(USING_X11_VULKAN OFF CACHE BOOL "" FORCE)
