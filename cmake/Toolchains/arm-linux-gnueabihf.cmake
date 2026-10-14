# Headless-only ARMv7 (32-bit, hard float) cross build, for testing the ARM32 JIT under qemu-arm.
# Used by ./b.sh --armhf.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR armv7-a)

# The sysroot defaults to where Debian/Ubuntu's cross packages put it. CROSS_ARMHF points at a root
# that the packages were extracted into instead (without installing them).
if(DEFINED ENV{CROSS_ARMHF})
	set(ARMHF_SYSROOT $ENV{CROSS_ARMHF}/usr/arm-linux-gnueabihf)
else()
	set(ARMHF_SYSROOT /usr/arm-linux-gnueabihf)
endif()

find_program(ARMHF_GCC NAMES arm-linux-gnueabihf-gcc-14 arm-linux-gnueabihf-gcc-15 arm-linux-gnueabihf-gcc REQUIRED)
string(REPLACE "-gcc" "-g++" ARMHF_GXX "${ARMHF_GCC}")
set(CMAKE_C_COMPILER ${ARMHF_GCC})
set(CMAKE_CXX_COMPILER ${ARMHF_GXX})
set(CMAKE_ASM_COMPILER ${ARMHF_GCC})

set(ARMHF_FLAGS "-march=armv7-a -mfpu=neon -mfloat-abi=hard")
set(CMAKE_C_FLAGS_INIT "${ARMHF_FLAGS}")
set(CMAKE_CXX_FLAGS_INIT "${ARMHF_FLAGS}")
set(CMAKE_ASM_FLAGS_INIT "${ARMHF_FLAGS}")

set(CMAKE_FIND_ROOT_PATH ${ARMHF_SYSROOT})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# No X11 in the sysroot; disable the X11 Vulkan path to avoid include-dir errors.
set(USING_X11_VULKAN OFF CACHE BOOL "" FORCE)

# See loongarch64-linux-gnu.cmake: a stub libGL.so and the GL headers come from
# cmake/scripts/setup-cross.sh, with b.sh's <build-dir>/stublibs as a local fallback.
set(OpenGL_GL_PREFERENCE LEGACY CACHE STRING "" FORCE)
set(CMAKE_EXE_LINKER_FLAGS
	"${CMAKE_EXE_LINKER_FLAGS} -L${CMAKE_BINARY_DIR}/stublibs"
	CACHE STRING "" FORCE)

find_program(QEMU_ARM NAMES qemu-arm-static qemu-arm)
if(QEMU_ARM)
	set(CMAKE_CROSSCOMPILING_EMULATOR "${QEMU_ARM};-L;${ARMHF_SYSROOT}" CACHE STRING "" FORCE)
endif()
