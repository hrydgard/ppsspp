# Building the libretro core

The sole runtime target is `ppsspp_libretro`. Configure with CMake and build that target,
or use `make -C libretro` with the appropriate `platform` setting.
FFmpeg is built from the vendored source with the same toolchain; prebuilt libraries are not used.

For development tests, configure with `-DUNITTEST=ON`, build `PPSSPPUnitTest`,
and run `PPSSPPUnitTest all`. Game fixtures must run through a libretro frontend.
The in-game PSP keyboard and PSP timing remain part of the core.

The Makefile requires a working CMake for the vendored FFmpeg source build.
In MSYS2 MINGW64 install it with `pacman -S mingw-w64-x86_64-cmake`.
It also detects working CMake installations bundled with Visual Studio on Windows.
An explicit `CMAKE=/path/to/cmake` override remains supported.
