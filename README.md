PPSSPP - a fast and portable PSP emulator
=========================================

Created by Henrik Rydgård

Additional code by many contributors, see the Credits screen

Originally released under the GPL 2.0 (and later) in November 2012

Official website:
https://www.ppsspp.org/

Discord:
https://discord.gg/5NJB6dD

No BIOS file required to play, PPSSPP is an "HLE" emulator.  Default settings balance good compatibility and speed.

To contribute, see [the development page](https://www.ppsspp.org/development.html).  Help testing, investigating, or fixing is always welcome.  See [the list of issues](https://github.com/hrydgard/ppsspp/issues).

For the latest source code, see [our github page](https://github.com/hrydgard/ppsspp).

For build instructions and other development tutorials, see [the wiki](https://github.com/hrydgard/ppsspp/wiki).

For game compatibility, see [community compatibility feedback](https://report.ppsspp.org/games).


What's new in 1.7.3/1.7.4/1.7.5
-------------------
* Fixes for a couple of common crashes
* Reverted immersive mode change temporarily to see if it helps misaligned buttons
* Change default adhoc server address

What's new in 1.7.2
-------------------
* Update text of "Buy PPSSPP Gold" button

What's new in 1.7.1
-------------------
* Minor crashfixes, lang updates
* Save bug fixed (#11508)

What's new in 1.7.0
-------------------
* Fix for insta-crash on Galaxy Note 9, some Xiaomi devices, and the new nVidia Shield (#11441)
* Vertex range culling on most platforms, fixes DTM: Race Driver and similar (#11393)
* Major speed boost in some Gundam and Earth Defense Force games (#10973)
* Fix for issues with post processing shaders in GL 3+ (#11182)
* Fixes to sound output switching on Windows (WASAPI) (#11438)
* Detects DirectInput devices when running (#11461)
* Simple Discord integration (#11307)
* New debugging APIs (#10909)
* Task switching made a lot more robust (fixes GPD XD problems) (#11447)
* Texture decoding optimizations (#11350)
* Tons and tons of miscellaneous bugfixes and compatibility fixes

What's new in 1.6.3
-------------------
* Crashfixes, task switching and one in Phantasy Star Portable
* Improve graphics in PoP on some devices

What's new in 1.6.1, 1.6.2
--------------------------
* Crashfixes
* Fix broken graphics in flOw.

What's new in 1.6.0
-------------------
* OpenGL backend now properly multithreaded, giving a good speed boost.
* Various Vulkan performance improvements (like #10911) and memory allocation fixes.
* GPU command interpreter performance improvements (#10658)
* Various fixes for app switching and widgets (#10855) on Android
* Bugfixes and some performance improvements in the ARM64 JIT compiler and IR interpreter
* Shader cache enabled for Vulkan
* Multiple iOS fixes, including JIT (#10465) and file browser (#10921).
* Improved compatibility on Mac (#10113)
* Texture replacement ID bugfix (note: some textures from 1.5.4 may become incompatible)
* Adhoc multiplayer fixes (#8975)
* Vulkan support on Linux/SDL (#10413)
* Retroarch support

What's new in 1.5.4
-------------------
* Bugfixes and crashfixes!

What's new in 1.5.0
-------------------
* Full Vulkan support, also for Android now. Very fast on supported devices. (#10033, #10049)
* Smarter graphics state management, reduced CPU consumption on all backends (#9899)
* Android: Support for Arabic and other scripts we couldn't support before
* Fix Android widgets, screen scaling (#10145)
* Fixes to video dumping
* Geometry problems fixed in Medal of Honor
* Implement immediate draws, fixing Thrillville (#7459)
* Software rendering improvements, speed and accuracy
* Hardware tesselation of PSP Beziers and Splines (used by a few games)
* Partial sceUsbGps and sceUsbCam support (Android)
* Android "Sustained performance mode" to avoid thermal throttling (#9901)
* Linux controller mapping fixes (#9997)
* Assorted bugfixes and compatibility improvements

Looking for [older news](history.md)?


Adhoc support
-------------
Not fully functional, but some games work.  Check the [Ad-Hoc section of the forum](http://forums.ppsspp.org/forumdisplay.php?fid=34) for help.

Credit goes to:
 - Igor Calabria
 - [coldbird's code](https://code.google.com/archive/p/aemu/)
 - Kyhel
