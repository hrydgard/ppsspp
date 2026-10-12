# Building the libretro core

The sole runtime target is `ppsspp_libretro`. Configure with CMake and build that target,
or use `make -C libretro` with the appropriate `platform` setting.

For development tests, configure with `-DUNITTEST=ON`, build `PPSSPPUnitTest`,
and run `PPSSPPUnitTest all`. Game fixtures must run through a libretro frontend.
The in-game PSP keyboard and PSP timing remain part of the core.
