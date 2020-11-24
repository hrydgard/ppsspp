if(NOT EXISTS "/usr/lib/aarch64-linux-gnu/tegra")
  message(FATAL_ERROR "Nvidia Jetson platform not recognized")
endif()

include_directories(SYSTEM
  /usr/include/GL
)

set(ARCH_FLAGS "-march=armv8-a+crc -mtune=cortex-a57 -funsafe-math-optimizations")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${ARCH_FLAGS}"  CACHE STRING "" FORCE)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${ARCH_FLAGS}" CACHE STRING "" FORCE)
set(CMAKE_ASM_FLAGS "${CMAKE_ASM_FLAGS} ${ARCH_FLAGS}" CACHE STRING "" FORCE)

set (CMAKE_EXE_LINKER_FLAGS "-Wl,-rpath,/usr/lib/aarch64-linux-gnu/tegra" CACHE STRING "" FORCE)

set(OPENGL_LIBRARIES  /usr/lib/aarch64-linux-gnu/tegra/libGLX_nvidia.so.0)
set(USING_X11_VULKAN ON CACHE BOOL "" FORCE)
