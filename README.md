

PPSSPP - a fast and portable PSP emulator
=========================================

Created by Henrik Rydgård

Additional code by many contributors, see the Credits screen

Originally released under the GPL 2.0 (and later) in November 2012

Official website:
https://www.ppsspp.org/

No BIOS file required to play, PPSSPP is in many ways a "HLE" emulator.

To contribute, see [the development page](https://www.ppsspp.org/development.html).

For the latest source code, see [our github page](https://github.com/hrydgard/ppsspp).

For build instructions and other development tutorials, see the [wiki](https://github.com/hrydgard/ppsspp/wiki).


This hackish branch add's:
 - frame profiler and simpler frame stats as a normal option,
 - adhoc between multiply instances on same pc,
 - some other ini-only options exposed in UI,
 - functionality of some additional hotkeys: W-Lan, Vol+/-, note(mute on/off), screen(accesses display layout editor),
 - different default settings personalized for my liking,
 - simple UI ~ an option to hide most rarely used options,
 - new awful compat hacks to deal with just as awful regressions and other problems,
 - guardband-culling branch as compat hack(activated for some games listed below),
 - some cwcheat workarounds of mine included in separate import-ready database,
 - a few more post process shaders,
 - texture replacement info from ge debugger(experimental aka usually works, but it might be done badly, available from right click menu),
 - ultra high render resolutions and render screenshots,
 - some untested/optional hack to avoid stutter on NVidia hardware/bad drivers.

 To list some game compatibility improvements here:
 - Armored Core and Bleach: Soul Carnival series(freezes),
 - Auditorium(black screen),
 - Bijin Tokei Portable(miniatures),
 - Driver '76(glitches),
 - DTM Race Driver/TOCA Race Driver/V8 Supercars games(glitches),
 - Evangelion Jo(most text in menus, note that it still requires pauth file;p),
 - Heroes Phantasia(flashing/z-fighting),
 - Hokuto no Ken: Raoh Gaiden(depth problem),
 - Nascar(glitches),
 - Nayuta no Kiseki(minor glitches),
 - Patapon 2(menu speed),
 - Resistance Retribution(some of the crashes),
 - Sangokushi IX with Power-Up Kit(black layer),
 - The Warriors(videos),
 - and more via included patches.

 Also includes a really awful hack for MOHH2 multiplayer(MOHH1 works fine without it;p).
 
 ~ LunaMoo

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

What's new in 1.4.2
-------------------
* Fixed longstanding bug causing games to crash on ARM64
* Software rendering crashfix, plus hide it for Android users
* D3D9 pixel offset bug fix (blurriness)
* Fixes for homebrew: Timing, MEMSIZE

What's new in 1.4.1
-------------------
* Fixes for some common hangs and crashes (#9698, ...)
* Vertex decoder optimizations (#9674)
* Corrections to mipmap bias and selection function (#9633)
* Major improvements and fixes to software renderer, including mipmap support (#9635, ...)
* UI background image support
* Basic mouse input support on Windows
* Windows desktop touch fixes (#9560)
* D3D11 "depal" color fixes, fixing Sonic Rivals
* Fix crash in framebuffer blits affecting Persona 3 (#9566)

What's new in 1.4-2
-------------------
* Bugfix release - build system didn't copy the flash0 directory to the APK on Android.

What's new in 1.4
-----------------
* Support D3D11 (performs better than OpenGL or D3D9 on most hardware)
* Audio quality improvement (linear interpolation) (#8950)
* Hardware spline/bezier tesselation in OpenGL, D3D11 and Vulkan (...)
* Post-processing shaders in D3D11
* Prescale UV setting removed, now the default (improves perf) (#9176)
* High DPI display fixes
* Various fixes for UMD switching for multi-UMD games (#9245, #9256)
* New audio setting to improve compatibility with Bluetooth headsets
* Various desktop gamepad compatibility fixes
* Workaround for mipmap issue, fixing fonts in Tactics Ogre Japanese
* Assorted minor compatibility fixes, code cleanup and performance improvements

Support for Symbian, Maemo and Blackberry has been removed.

What's new in 1.3.0
-------------------
* Fix JIT problems on Galaxy S7 and iOS 9+ devices. (#8965, #8937)
* Fix Android TV support and use latest FFmpeg. (#8651, #8870)
* Texture replacement support - for custom textures and upscaling. (#8715, #8821)
* Initial game recording / TAS features. (#8939, #8945)
* Correctly map memory on Raspberry Pi 3, much better performance. (#8801)
* Workaround rendering issues on Tegra K1/X1. (8294a54)
* Disc streaming to play quickly from tablet/phone on wifi. (#8845)
* Initial Vulkan support - not full featured yet. (#8601, etc.)
* Experimental new CPU backend and CPU fixes. (#8725)
* Allow insert/eject of memstick - required by some games. (#8889)
* Better support for ps3 controller mapping. (#8949)
* Better UI handling for settings with long names in some languages. (#8900, #8898)
* Screenshots in compatibility reporting, better website. (#8812)
* Fix type D cheat codes, allow for homebrew. (#8818)
* Graphic glitch fixes in several games. (#8686, #8757, #8804)
* Fix video playback glitches in several games. (#8793, #8803, #8867, #8914)
* Various performance and compatibility improvements. (#8868, #8884, #8932, #8934, #8813, #8701, #8960)
* Various debugger and GE debugger improvements. (#8882, #8762)
* Fix some problems when switching away from and back to the app (#8968)

What's new in 1.2.2
-------------------
* Went back to the old way of initializing graphics on Android. Should fix many recent issues.
* Some graphical fixes, a vertex cache performance improvement and a screen clear optimization
* Fix for dual source blending on most SHIELD devices, causing graphical issues.
* Fix the homebrew store incorrectly unzipping some games. This will lead to more games being added.
* Slightly faster ISO handling

What's new in 1.2.1
-------------------
* Fixes for some crash-on-shutdown and app switching problems.

What's new in 1.2.0
-------------------
* A major rework of sceAtrac audio decoding, fixing various music hangs and similar issues
* Many fixes and workarounds to depth and stencil buffer usage, and also FBO management
* Audio reverb support
* Combo keys - custom touch buttons that press multiple PSP buttons
* 5xBR upscaling on GPU (postprocessing effect)
* Fix problems with playback of video with mono audio
* Performance improvements like multithreaded audio mixing
* ARM64 JIT crash bug fixes
* GLSL shader cache to reduce stuttering ingame
* Support render-to-CLUT functionality that some games use to change colors of various monsters
* x86-64 support on Android
* Auto-hide on-screen controls after a while of no usage
* Fixes to prescale UV speedhack, now seems reliable
* Faster ISO RAM cache
* New UI for moving around the PSP display on larger screens
* Minor UI fixes like better slider controls
* Assorted stability fixes (ffmpeg crash, etc)
* Volume setting is back
* Preparations for supporting more graphics APIs
* AdHoc port offset
* Support another HD remaster (Sora no Kiseki Kai HD)

What's new in 1.1.1
-------------------
* Fixed new crash in Persona and other games on ARM64, like Galaxy S6
* Fixed crashes when trying to launch web browser when not present on Android, like on Android TV
* Fix crash in games that used "depal" functionality (OpenGL)
* Fixed rounding mode problems in the JITs
* Fix crash when loading savestates many times, and savestate compatibility on Android-x86
* Fix minor glitch in Ridge Racer

What's new in 1.1
-----------------
* Support for ARM64 on Android, for improved performance on new devices. Has some new optimizations.
* Support Android TV, like nVidia Shield TV
* Screen rotation on PC, useful for vertical games like Star Soldier
* Many minor performance improvements and compatibility bug fixes
* GPU emulation fixes like correct depth rounding, fixing text in Phantasy Star
* Other graphical fixes like UV rotation
* Support savestates for homebrew apps
* Simple integrated "Homebrew Store" to download PSP homebrew apps
* Minor AdHoc multiplayer improvements. Still many issues left.
* Disable a dangerous optimization on ARM, causing walk-through-walls in Tenchu
* sceAtrac music compatibility fixes, fixing noise in a few games
* Better texture scaling performance
* Direct3D closer in features to OpenGL
* Works better on BSD operating systems
* Savedata management UI

What's new in 1.0.1
-------------------
* Bugfixes like the save state scroll issue, cosmetic issues like overscroll
* Some cheat code bugfixes
* Adler32 and Mersenne Twister modules added (fixes some obscure games)
* Fix for Jak & Daxter slowdown
* Graphics hack for Phantasy Star Portable 2 for Direct3D9
* Fix compatibility with some PowerVR devices broken since v0.9.5-959-g4998044

What's new in 1.0
-----------------
* Many, many bug fixes in JIT and elsewhere, improving compatibility
* Proper fix for Zenfone and related devices
* Direct3D 9 supported as a rendering backend on Windows, helps on old GPUs and can be faster than OpenGL in many cases
* You can now create specific configs per game
* FPU rounding modes much better supported, fixes the Peace Walker boss that was undefeatable. NOTE: This breaks saves in Gods Eater - you must turn off the better rounding, load your save game, turn it on and save.
* The JIT now uses SSE on x86, improving speed considerably. This does not affect ARM devices, that's for the next version or two.
* Improved audio output code on both Windows and Android, reducing audio latency on Windows and on some Android systems
* FFMPEG upgraded, fixed some music hangs ("GHA phase shifts")
* Some Ad Hoc improvements, coldbird.net is now default adhoc server. Ad Hoc still unfinished and hard to use.
* Graphics fixes: Bezier/spline drawing fixes, vertex position fixes, DanganRonpa on Adreno fixed, flat shading fixed, vertex cache improved, some PowerVR blockiness issues fixed, screen scaling filter added, Google Cardboard support
* Simulate UMD speed better, fixing hangs in several games
* More Atrac3 fixes, fixing hangs
* Somewhat better disk full handling
* Fixes to dynamic unloading of code, fixing problems in GEB and TRM 2/3
* Updated to SDL2 where applicable (Linux, Mac)
* Some new features, like analog/dpad-swap hotkey, graphics hack for Phantasy Star, show last bit of debug log in dev tools, etc
