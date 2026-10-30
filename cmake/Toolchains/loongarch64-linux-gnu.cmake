set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR loongarch64)

set(CMAKE_C_COMPILER loongarch64-linux-gnu-gcc-14)
set(CMAKE_CXX_COMPILER loongarch64-linux-gnu-g++-14)
set(CMAKE_ASM_COMPILER loongarch64-linux-gnu-gcc-14)

set(CMAKE_FIND_ROOT_PATH /usr/loongarch64-linux-gnu)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
# pkg-config ignores the root path and would hand back the host's libraries (the host's curl, say).
set(ENV{PKG_CONFIG_LIBDIR} /usr/loongarch64-linux-gnu/lib/pkgconfig:/usr/loongarch64-linux-gnu/share/pkgconfig)

# No X11 in the sysroot; disable the X11 Vulkan path to avoid include-dir errors.
set(USING_X11_VULKAN OFF CACHE BOOL "" FORCE)

# Use legacy GL preference so find_package(OpenGL) looks for a single libGL.so
# rather than the GLVND split (libOpenGL.so + libGLX.so), which we don't have.
# A stub libGL.so + GL headers must be present in the sysroot; they are placed
# there by cmake/scripts/setup-cross.sh (run once with sudo, or automatically
# by the CI install step).
set(OpenGL_GL_PREFERENCE LEGACY CACHE STRING "" FORCE)

# b.sh also creates a stub in <build-dir>/stublibs/ as a local fallback so the
# linker can resolve GL/GLX calls from GLEW's static archive via PLT entries.
set(CMAKE_EXE_LINKER_FLAGS
    "${CMAKE_EXE_LINKER_FLAGS} -L${CMAKE_BINARY_DIR}/stublibs"
    CACHE STRING "" FORCE)

# Allow CMake's compile-check programs to run via QEMU, if it's installed.
# Debian/Ubuntu ship the static binaries in qemu-user-static and the dynamic
# ones in qemu-user; either will do, so take whichever is present.
find_program(QEMU_LOONGARCH64 NAMES qemu-loongarch64-static qemu-loongarch64)
if(QEMU_LOONGARCH64)
    # The /lib64 symlink is created by the setup script; without it, pass -L
    # explicitly so QEMU can find the loongarch64 dynamic linker.
    if(EXISTS "/lib64/ld-linux-loongarch-lp64d.so.1")
        set(CMAKE_CROSSCOMPILING_EMULATOR "${QEMU_LOONGARCH64}" CACHE STRING "" FORCE)
    else()
        set(CMAKE_CROSSCOMPILING_EMULATOR
            "${QEMU_LOONGARCH64};-L;/usr/loongarch64-linux-gnu"
            CACHE STRING "" FORCE)
    endif()
endif()
