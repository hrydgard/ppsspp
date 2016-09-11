PPSSPP - a fast and portable PSP emulator
=========================================

Created by Henrik Rydgård

Additional code by many contributors, see the Credits screen

Originally released under the GPL 2.0 (and later) in November 2012

Official website:
http://www.ppsspp.org/

No BIOS file required to play, PPSSPP is in many ways a "HLE" emulator.

To contribute, see [the development page](http://www.ppsspp.org/development.html).

For the latest source code, see [our github page](https://github.com/hrydgard/ppsspp).

For build instructions and other development tutorials, see the [wiki](https://github.com/hrydgard/ppsspp/wiki).

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



ADHOC SUPPORT (by Igor Calabria)
================================
This is based on coldbird's code: http://code.google.com/p/aemu/
All credit goes to him!

Status
------
Code is a bit of a mess and it's not fully functional yet, I still need to implement
some functions and add a upnp lib(really important for people with routers).

I did test it with some games(emulator <-> real psp with the server running locally)
and it's looking good:

* Worms Open Warfare: Ran just fine, I was able to play a whole match without problems
* Monster Hunter Freedom Unite: Runs fine too. Gathering Hall and embarking on quests works
* Dissidia Duodecim 012: Doesn't work. It requires some functions that I haven't implemented
yet. Also, it uses a port < 1000 and thats reserved for admin apps on linux, running the emu
as sudo "solves" it, but it's far from ideal.
* Pacman World Rally: Works too.

Update (Kyhel) :
---------------
* Now compiles on both Mac OSX and Windows. For more details on how to play and build
go to see there http://forums.ppsspp.org/showthread.php?tid=3595&pid=59241#pid59241
* Got it tested windows <-> mac osx <-> psp, it works
* Monster Hunter 3rd HD works too, as well as God Eater Burst.
