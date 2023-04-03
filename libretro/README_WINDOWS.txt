Install msys.

Follow the guide here to set up your MSYS installation for retroarch compilation:

https://docs.libretro.com/development/retroarch/compilation/windows/

You can probably really skip most of the packages but you need make:

pacman -S make

"Install" the plugin in Retroarch.

Then use the following in msys:

cd libretro

make platform=windows_msvc2019_desktop_x64 -j32 && cp ppsspp_libretro.* /d/retroarch/cores

Note that the latter part copies the DLL/PDB into wherever retroarch reads it from. Might need to adjust the path,
and adjust -j32 depending on your number of logical CPUs - might not need that many threads (or you might need more...).

Also, the "2019" part has no significance, it seems - it's fine even if you're on MSVC 2022.

(plain make without a platform parameter doesn't work - g++ isn't able to build the D3D11 stuff, or at least it fails to link).

To debug from within MSVC, open retroarch.exe (or retroarch_debug.exe) as a Project/Solution, then open a few of the cpp files,
set some breakpoints and just launch using F5.

Useful libretro/vulkan sample code:

https://github.com/libretro/libretro-samples/blob/master/video/vulkan/vk_rendering/libretro-test.c
