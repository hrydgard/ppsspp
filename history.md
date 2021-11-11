This is history moved out from README.md, which was getting a bit long.

What's new in 1.9.0/1.9.3
-------------------------
* Sizing/touch fixes on Android
* Fixes for some crashes

What's new in 1.9.0/1.9.2
-------------------------
* Flicker fixed in God of War that appeared with newer drivers for Mali GPUs ([#12224])
* Improve performance of God of War on Vulkan ([#12242]), implement built-in GPU profiler ([#12262], [#12266])
* Vertex range culling fixed on ARM Mali ([#12227])
* Started to improve VFPU precision, resulting so far in a fix for the long standing Tekken 6 leg shaking problem ([#12217])
* Fixed a VFPU precision snafu on ARM64, fixing disappearing officers in Warriors Orochi ([#11299]) and some problems in Tomb Raider ([#11179]).
* Vulkan is the default again on Android versions newer than Pie
* Fix various homebrew store issues
* GPU pause signal handling fixed, fixing some hangs in Bleach and Armored Core games ([#12160])
* Audio sample rate conversion handling fixes ([#12147])
* Some Vulkan optimizations (pre-rotation ([#12216]), perf fix in Metal Gear Acid, etc)
* Multiple fixes for the UWP build ([#12036], others)
* MP3 playback fixes ([#12005])
* Audio in Motorstorm: Arctic Edge fixed by returning errors correctly ([#12121])
* Audio glitches in Final Fantasy Tactics fixed ([#9956])
* Camera display in Invizimals fixed ([#12278], [#8521])
* Added hotkeys for texture dump and replace ([#11938])
* Added Visual Studio 2019 support. Windows XP is no longer supported ([#11995], others)
* Fixes for video capture ([#12069])
* Added a separate sound volume for alternative speed ([#12124])
* Improved mouse control (Windows only) ([#12173], [#12176])
* Support for installing texture packs and ISOs from zips ([#12175])
* Right analog support for touch controls (only used by patched games and some HD remasters) ([#12182])
* Android: Fix OpenSL initialization, possibly helps audio crackle a little. ([#12333]).
* Fix graphics on Amazon Fire TV Stick 4K
* Fixed strange vehicle behavior in MGS:PW (somehow) ([#12342])

What's new in 1.8.0
-------------------
* Avoid crash when starting PPSSPP due to bad AMD Vulkan drivers ([#11802])
* PowerVR Vulkan display corruption fixed ([#11744], [#11747])
* Naruto Shippuden 3 hang fixed ([#11780])
* Fixes to various lighting bugs ([#11567], [#11574], [#11577])
* Fix control issue in Sonic Rivals and Rock Band ([#11878], [#11798], [#11879])
* Significant performance improvement in Earth Defense Force 2 ([#11172])
* Fix "real clock sync" setting (helps with latency for music games - [#11813])
* More speed in FF4 effects and other generated curves ([#11425])
* Support for resizing Vulkan on Linux ([#11451])
* Improved support for GLES on Linux/IoT ([#11507], [#11537], [#11541], [#11632], [#11746], [#11806], [#11845])
* Percentage based frameskipping ([#11523])
* DXT accuracy improved, fixing thick white line in Gran Turismo sky ([#11530])
* Fix Motorstorm freeze on non-Windows ([#11618])
* Faster block transfer in some games like Digimon Adventures ([#11531], [#11553], [#11776])
* Blending optimizations and improvements ([#11543], [#11620])
* Improve D3D11 rendering issues ([#11584])
* Change default graphics backend to D3D11 or OpenGL ([#11621], [#11658])
* Remove some outdated settings ([#11665], [#11666], [#11667])
* Fix remote disc streaming with ipv6 ([#11689], [#11700])
* Vulkan: Workarounds for some driver bugs for 5xx series Qualcomm GPUs ([#11691], [#11694])
* Fix some Qt port issues with recent performance improvements ([#11720], [#11807], [#11808])
* UWP Xbox One: fix X/Back button confusion ([#11740])
* Fix Formula 1 2006 timing issue ([#11767])
* Fixes and workarounds for some vertex range culling bugs that broke a few games ([#11785], [#11859]), and disable it on older GPUs ([#11712], [#11773], [#11787])
* Android: Allow putting PSP storage on custom paths like SD cards ([#11812])
* Corrected vocp instruction, fixing models in Artdink games ([#11822], [#11835])
* Bundle SDL in binary for macOS builds ([#11831])
* Prevent keeping games open on Windows ([#11842])

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
* Save bug fixed ([#11508])

What's new in 1.7.0
-------------------
* Fix for insta-crash on Galaxy Note 9, some Xiaomi devices, and the new nVidia Shield ([#11441])
* Vertex range culling on most platforms, fixes DTM: Race Driver and similar ([#11393])
* Major speed boost in some Gundam and Earth Defense Force games ([#10973])
* Fix for issues with post processing shaders in GL 3+ ([#11182])
* Fixes to sound output switching on Windows (WASAPI) ([#11438])
* Detects DirectInput devices when running ([#11461])
* Simple Discord integration ([#11307])
* New debugging APIs ([#10909])
* Task switching made a lot more robust (fixes GPD XD problems) ([#11447])
* Texture decoding optimizations ([#11350])
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
* Various Vulkan performance improvements (like [#10911]) and memory allocation fixes.
* GPU command interpreter performance improvements ([#10658])
* Various fixes for app switching and widgets ([#10855]) on Android
* Bugfixes and some performance improvements in the ARM64 JIT compiler and IR interpreter
* Shader cache enabled for Vulkan
* Multiple iOS fixes, including JIT ([#10465]) and file browser ([#10921]).
* Improved compatibility on Mac ([#10113])
* Texture replacement ID bugfix (note: some textures from 1.5.4 may become incompatible)
* Adhoc multiplayer fixes ([#8975])
* Vulkan support on Linux/SDL ([#10413])
* Retroarch support

What's new in 1.5.4
-------------------
* Bugfixes and crashfixes!

What's new in 1.5.0
-------------------
* Full Vulkan support, also for Android now. Very fast on supported devices. ([#10033], [#10049])
* Smarter graphics state management, reduced CPU consumption on all backends ([#9899])
* Android: Support for Arabic and other scripts we couldn't support before
* Fix Android widgets, screen scaling ([#10145])
* Fixes to video dumping
* Geometry problems fixed in Medal of Honor
* Implement immediate draws, fixing Thrillville ([#7459])
* Software rendering improvements, speed and accuracy
* Hardware tesselation of PSP Beziers and Splines (used by a few games)
* Partial sceUsbGps and sceUsbCam support (Android)
* Android "Sustained performance mode" to avoid thermal throttling ([#9901])
* Linux controller mapping fixes ([#9997])
* Assorted bugfixes and compatibility improvements

What's new in 1.4.2
-------------------
* Fixed longstanding bug causing games to crash on ARM64
* Software rendering crashfix, plus hide it for Android users
* D3D9 pixel offset bug fix (blurriness)
* Fixes for homebrew: Timing, MEMSIZE

What's new in 1.4.1
-------------------
* Fixes for some common hangs and crashes ([#9698], ...)
* Vertex decoder optimizations ([#9674])
* Corrections to mipmap bias and selection function ([#9633])
* Major improvements and fixes to software renderer, including mipmap support ([#9635], ...)
* UI background image support
* Basic mouse input support on Windows
* Windows desktop touch fixes ([#9560])
* D3D11 "depal" color fixes, fixing Sonic Rivals
* Fix crash in framebuffer blits affecting Persona 3 ([#9566])

What's new in 1.4-2
-------------------
* Bugfix release - build system didn't copy the flash0 directory to the APK on Android.

What's new in 1.4
-----------------
* Support D3D11 (performs better than OpenGL or D3D9 on most hardware)
* Audio quality improvement (linear interpolation) ([#8950])
* Hardware spline/bezier tesselation in OpenGL, D3D11 and Vulkan (...)
* Post-processing shaders in D3D11
* Prescale UV setting removed, now the default (improves perf) ([#9176])
* High DPI display fixes
* Various fixes for UMD switching for multi-UMD games ([#9245], [#9256])
* New audio setting to improve compatibility with Bluetooth headsets
* Various desktop gamepad compatibility fixes
* Workaround for mipmap issue, fixing fonts in Tactics Ogre Japanese
* Assorted minor compatibility fixes, code cleanup and performance improvements

Support for Symbian, Maemo and Blackberry has been removed.

What's new in 1.3.0
-------------------
* Fix JIT problems on Galaxy S7 and iOS 9+ devices. ([#8965], [#8937])
* Fix Android TV support and use latest FFmpeg. ([#8651], [#8870])
* Texture replacement support - for custom textures and upscaling. ([#8715], [#8821])
* Initial game recording / TAS features. ([#8939], [#8945])
* Correctly map memory on Raspberry Pi 3, much better performance. ([#8801])
* Workaround rendering issues on Tegra K1/X1. (8294a54)
* Disc streaming to play quickly from tablet/phone on wifi. ([#8845])
* Initial Vulkan support - not full featured yet. ([#8601], etc.)
* Experimental new CPU backend and CPU fixes. ([#8725])
* Allow insert/eject of memstick - required by some games. ([#8889])
* Better support for ps3 controller mapping. ([#8949])
* Better UI handling for settings with long names in some languages. ([#8900], [#8898])
* Screenshots in compatibility reporting, better website. ([#8812])
* Fix type D cheat codes, allow for homebrew. ([#8818])
* Graphic glitch fixes in several games. ([#8686], [#8757], [#8804])
* Fix video playback glitches in several games. ([#8793], [#8803], [#8867], [#8914])
* Various performance and compatibility improvements. ([#8868], [#8884], [#8932], [#8934], [#8813], [#8701], [#8960])
* Various debugger and GE debugger improvements. ([#8882], [#8762])
* Fix some problems when switching away from and back to the app ([#8968])

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

What's new in 0.9.9.1
-------------------
A few issues have been discovered in the release that need fixing, the Star Ocean fix had a bug and there are some unexpected slowdowns.

Improved sceMpegRingbufferAvailableSize -  UFC© Undisputed​™ 2010 now playable

Improved ISO File System - Bleach Soul Carnival 2 now in-game (but freeze when in menu)

What's new in 0.9.9
-------------------
* CLUT (paletted) texturing from framebuffers supported, fixing many graphical issues
  like the shadows in Final Fantasy: Type-0
* More types of framebuffer copies are now handled correctly, fixing a variety of graphical
  issues, like the sun in Burnout and many more
* Better savedata compatibility with the real PSP
* Support for more codecs used by "Custom BGM" and sometimes regular music in games: MP3, AAC
* PMP video format support
* Implemented some strange blending modes like ABSDIFF as shaders, fixing the outlines in DBZ Tag Team and more.
* Emulation of the vrot CPU instruction improved - it caused cracks in FF3 graphics before
* Many bugfixes around the UI, touch D-pad now works better when scaled large
* Workaround implemented to support Star Ocean's stencil trickery as efficiently as possible on all platforms
* Major corrections to module loading and memory management, fixing further games
* Bulgarian and Thai language translations were contributed
* Many, many more game fixes and bug fixes

What's new in 0.9.8
-------------------
* OpenGL ES 3 detection bug on Xperia devices fixed, graphics work again.
* More accurate audio mixing and emulation
* Software rendering and display list performance improvements
* Workaround for timing issue hanging Crash Tag Team Racing
* Galician language
* Built-in ARM disassembler improvements (dev feature)
* Fix for immersive mode volume key issue on Android Kitkat
* And more minor tweaks and fixes as always.

What's new in 0.9.7.1
---------------------
* Some critical bugfixes (rotation, haptic feedback on Android, etc)

What's new in 0.9.7
-------------------
* Several scheduling and audio fixes, fixing black screens in Yu Gi Oh games among other things.
* Screen rotation and immersive mode support on Android
* Large improvements to the software renderer (still not really playable, but looks right more
  often than not)
* New VPL allocator and bugfixes, fixing Pangya Golf performance problems.
* Some mpeg/video playback fixes, fixing Parappa The Rapper and others. Some issues remain.
* Fix save state bugs causing incompatibility between 32 and 64-bit platforms.
* Symbol map/debugger improvements
* Depth buffer copy, fixing Jeanne D'arc. May cause minor slowdowns though, this will be worked
  around in the future.
* MsgDialog fixes. Saving fixed in numerous games.
* Initial multitouch support on Windows 8 for on-screen controls.

What's new in 0.9.6
-------------------
* Large general speed improvements and assorted bug fixes
* "Software Skinning" option which speeds up many games with animated 3D characters
  (but may slow down a few, like Monster Hunter games - experiment with turning it off)
* Various fixes around stencil/alpha, reducing glow problems in Wipeout and Gods Eater Burst.
* Timing improvements making more games run at the correct FPS, also fixing some audio issues
* More debugger features
* Option for four-way touch dpad, avoiding diagonal dpad issues
* Better looking and individually resizable touch controls
* Add ability to switch UMD in multi-disc games (works for most)
* Emulate PSP-2000 rather than the 1000 model by default. Not much different in practice.
* Automatic install of games from ZIP files, like demos and many homebrew.
* VERY basic ad-hoc online play support, to be improved in future versions. See below.
* Software renderer improvements

What's new in 0.9.5
-------------------
* Many, many emulation fixes:
  - bezier/spline curve support, fixing LocoRoco and others
  - stencil clear emulation, fixing Final Fantasy IV text
* Performance improvements in some games
* Post-processing shaders like FXAA, scanlines, vignette
* More solid save states (we will try to keep them working from now on. Save states only upgrade forward,
  not backward to older versions though).
* Change render resolution independently of window size
* Massive debugger improvements
* Win32 menu bar is now translatable
* Multiple UI bugs were fixed, and the UI instantly changes when a new language is selected
* Win32: Ability to store PPSSPP's config files and memory stick files in places other than the same directory
* Android-x86 support
* Unofficial port for modified Xbox 360 consoles
* Atrac3+ plugin no longer required. Symbian now supports Atrac3+ audio.
* Symbian audio and ffmpeg is now threaded for more consistent media processing.
* Haptic feedback support for mobile devices.
* Accurate system information for mobile devices.
* Qt audio has been fixed.
* Analog controller support for Blackberry.


ADHOC SUPPORT (by Igor Calabria)
================================
This is based on coldbird's code: https://code.google.com/archive/p/aemu/
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


[comment]: # (LINK_LIST_BEGIN_HERE)
[#10911]: https://github.com/hrydgard/ppsspp/issues/10911 "Vulkan: Depalettize in shaders"
[#10658]: https://github.com/hrydgard/ppsspp/issues/10658 "Execute_Prim: Add a smaller \"inner interpreter\" to speed long up sequences of PRIM commands"
[#10855]: https://github.com/hrydgard/ppsspp/issues/10855 "PPSSPP 1.5.4 on Android. Shortcuts do not work the first time."
[#10465]: https://github.com/hrydgard/ppsspp/issues/10465 "Allow arm64 jit when memory base is not nicely aligned"
[#10921]: https://github.com/hrydgard/ppsspp/issues/10921 "Add support for File app (file browser) on iOS 11"
[#10113]: https://github.com/hrydgard/ppsspp/issues/10113 "Request a core profile in OpenGL (after 1.5.0)"
[#8975]: https://github.com/hrydgard/ppsspp/issues/8975 "Ad hoc: Band-aid fix for clear peer list. Should fix #7331"
[#10413]: https://github.com/hrydgard/ppsspp/issues/10413 "SDL/X11 Vulkan init"
[#10033]: https://github.com/hrydgard/ppsspp/issues/10033 "VulkanRenderManager - big refactoring of the Vulkan code"
[#10049]: https://github.com/hrydgard/ppsspp/issues/10049 "Vulkan threading tweaks and minor"
[#9899]: https://github.com/hrydgard/ppsspp/issues/9899 "Vulkan state optimizations"
[#10145]: https://github.com/hrydgard/ppsspp/issues/10145 "Android: When creating shortcuts, put the file path in data, not in extras."
[#7459]: https://github.com/hrydgard/ppsspp/issues/7459 "Thrillville Off The Rails Graphics' issues"
[#9901]: https://github.com/hrydgard/ppsspp/issues/9901 "Initial work on supporting sustained perf mode"
[#9997]: https://github.com/hrydgard/ppsspp/issues/9997 "Create default mapping for unknown control pads in SDL's controller database"
[#9698]: https://github.com/hrydgard/ppsspp/issues/9698 "General crash and hang fixes"
[#9674]: https://github.com/hrydgard/ppsspp/issues/9674 "Implement a few x86 vertexjit optimizations"
[#9633]: https://github.com/hrydgard/ppsspp/issues/9633 "Improve support for mipmaps and related headless fixes"
[#9635]: https://github.com/hrydgard/ppsspp/issues/9635 "SoftGPU: Rasterize triangles in chunks of 4 pixels"
[#9560]: https://github.com/hrydgard/ppsspp/issues/9560 "Allow using any touchId to scroll. Should help #9554."
[#9566]: https://github.com/hrydgard/ppsspp/issues/9566 "Fix out-of-bounds framebuffer blit on color bind"
[#8950]: https://github.com/hrydgard/ppsspp/issues/8950 "SasAudio: Implement linear interpolation"
[#9176]: https://github.com/hrydgard/ppsspp/issues/9176 "Always prescale uv (\"texcoord speedhack\")"
[#9245]: https://github.com/hrydgard/ppsspp/issues/9245 "Fix \"Akaya Akashiya Ayakashi\" umd switch"
[#9256]: https://github.com/hrydgard/ppsspp/issues/9256 "Fix Dies irae Amantes amentes umd switch"
[#8965]: https://github.com/hrydgard/ppsspp/issues/8965 "Port over the Exynos cacheline size fix from Dolphin."
[#8937]: https://github.com/hrydgard/ppsspp/issues/8937 "Implement W^X-compatible JIT. Hopefully makes JIT work on iOS again"
[#8651]: https://github.com/hrydgard/ppsspp/issues/8651 "Switch Android build to using clang (needs buildbot update)"
[#8870]: https://github.com/hrydgard/ppsspp/issues/8870 "Update to target Android SDK platform 24 (N)"
[#8715]: https://github.com/hrydgard/ppsspp/issues/8715 "Initial texture replacement support"
[#8821]: https://github.com/hrydgard/ppsspp/issues/8821 "Add basic TextureReplacement UI options."
[#8939]: https://github.com/hrydgard/ppsspp/issues/8939 "Add Frame Advance"
[#8945]: https://github.com/hrydgard/ppsspp/issues/8945 "Add Display Recording and Audio Dumping to Desktop"
[#8801]: https://github.com/hrydgard/ppsspp/issues/8801 "Improve performance on Raspberry Pi (and maybe other Linux)"
[#8845]: https://github.com/hrydgard/ppsspp/issues/8845 "Add UI for wifi remote disc streaming"
[#8601]: https://github.com/hrydgard/ppsspp/issues/8601 "Vulkan rendering backend. Early Work-In-Progress"
[#8725]: https://github.com/hrydgard/ppsspp/issues/8725 "IR Interpreter"
[#8889]: https://github.com/hrydgard/ppsspp/issues/8889 "Initial support for memstick insert/remove"
[#8949]: https://github.com/hrydgard/ppsspp/issues/8949 "Adds --PS3 comand line option for PS3 controller support on SDL."
[#8900]: https://github.com/hrydgard/ppsspp/issues/8900 "Wrap text in many places where it makes sense"
[#8898]: https://github.com/hrydgard/ppsspp/issues/8898 "UI: Scale option text down when there's no space"
[#8812]: https://github.com/hrydgard/ppsspp/issues/8812 "Include screenshots in compatibility reports"
[#8818]: https://github.com/hrydgard/ppsspp/issues/8818 "CWC improvements/fixes"
[#8686]: https://github.com/hrydgard/ppsspp/issues/8686 "Interpolate bezier patch colors/UVs using bernstein"
[#8757]: https://github.com/hrydgard/ppsspp/issues/8757 "Save FBOs on decimate when a safe size is known"
[#8804]: https://github.com/hrydgard/ppsspp/issues/8804 "Download single-use renders right away"
[#8793]: https://github.com/hrydgard/ppsspp/issues/8793 "Correct some scePsmf info retrieval funcs and error handling"
[#8803]: https://github.com/hrydgard/ppsspp/issues/8803 "Reject invalid MPEG puts for certain lib versions"
[#8867]: https://github.com/hrydgard/ppsspp/issues/8867 "Improve mpeg parsing / corruption issues"
[#8914]: https://github.com/hrydgard/ppsspp/issues/8914 "Psmf: Ignore stream size with old PsmfPlayer libs"
[#8868]: https://github.com/hrydgard/ppsspp/issues/8868 "Return error when trying to close files with pending operations"
[#8884]: https://github.com/hrydgard/ppsspp/issues/8884 "Better support zh_gb.pgf"
[#8932]: https://github.com/hrydgard/ppsspp/issues/8932 "Font: Draw nothing for chars before first glyph"
[#8934]: https://github.com/hrydgard/ppsspp/issues/8934 "Io: Do not delay on file seek"
[#8813]: https://github.com/hrydgard/ppsspp/issues/8813 "Enable extra ram for The Elder Scrolls Travels Oblivion USA Beta and Melodie alpha"
[#8701]: https://github.com/hrydgard/ppsspp/issues/8701 "Correct dependency handling when loading modules"
[#8960]: https://github.com/hrydgard/ppsspp/issues/8960 "Fix scePowerSetClockFrequency timing"
[#8882]: https://github.com/hrydgard/ppsspp/issues/8882 "Add custom log expressions to the debugger"
[#8762]: https://github.com/hrydgard/ppsspp/issues/8762 "Add more GE debugger features"
[#8968]: https://github.com/hrydgard/ppsspp/issues/8968 "Android: Add both a lost and restore phase"
[#12224]: https://github.com/hrydgard/ppsspp/issues/12224 "Vulkan: Add missing barrier between multiple passes to the same target."
[#12242]: https://github.com/hrydgard/ppsspp/issues/12242 "Vulkan: Automatically merge render passes to the same target when possible"
[#12262]: https://github.com/hrydgard/ppsspp/issues/12262 "Vulkan: Implement basic integrated GPU profiling."
[#12266]: https://github.com/hrydgard/ppsspp/issues/12266 "Vulkan: Further improvements to GPU profiling"
[#12227]: https://github.com/hrydgard/ppsspp/issues/12227 "Vulkan/GL: Set all four coordinates to NaN instead of just W."
[#12217]: https://github.com/hrydgard/ppsspp/issues/12217 "Merge vfpu-dot changes and add compat flag for Tekken"
[#11299]: https://github.com/hrydgard/ppsspp/issues/11299 "Warrior Orochi Enemy Missing"
[#11179]: https://github.com/hrydgard/ppsspp/issues/11179 "Tomb Raider Anniversary jump to horizontal bar issue"
[#12160]: https://github.com/hrydgard/ppsspp/issues/12160 "GPU: Forget pause signal on new list"
[#12147]: https://github.com/hrydgard/ppsspp/issues/12147 "Handle audio SRC mixing more correctly"
[#12216]: https://github.com/hrydgard/ppsspp/issues/12216 "Cant get Invizimals to work on PPSSPP (Video source not rendered)"
[#12036]: https://github.com/hrydgard/ppsspp/issues/12036 "Several UWP fixes"
[#12005]: https://github.com/hrydgard/ppsspp/issues/12005 "Correct mp3 looping, frame num, and sum decoded"
[#12121]: https://github.com/hrydgard/ppsspp/issues/12121 "Return errors on Audio2 release when channel busy"
[#9956]: https://github.com/hrydgard/ppsspp/issues/9956 "Audio crackle/distortion in Final Fantasy Tactics"
[#12278]: https://github.com/hrydgard/ppsspp/issues/12278 "UsbCam/jpeg: Cleanups, notify framebuffer manager"
[#8521]: https://github.com/hrydgard/ppsspp/issues/8521 "PSP Camera Support (Android)"
[#11938]: https://github.com/hrydgard/ppsspp/issues/11938 "Add texture dump/replace hotkeys."
[#11995]: https://github.com/hrydgard/ppsspp/issues/11995 "Fix VS2019 builds and remove _xp dependency"
[#12069]: https://github.com/hrydgard/ppsspp/issues/12069 "Fix avi dump feature"
[#12124]: https://github.com/hrydgard/ppsspp/issues/12124 "Audio: Add volume for alternate speed"
[#12173]: https://github.com/hrydgard/ppsspp/issues/12173 "Ignore mapped mouse input for UI"
[#12176]: https://github.com/hrydgard/ppsspp/issues/12176 "Mouse improvements"
[#12175]: https://github.com/hrydgard/ppsspp/issues/12175 "UI: Allow installing texture packs from zips"
[#12182]: https://github.com/hrydgard/ppsspp/issues/12182 "Add right analog for touch controls."
[#12333]: https://github.com/hrydgard/ppsspp/issues/12333 "Android OpenSL initial queue fix"
[#12342]: https://github.com/hrydgard/ppsspp/issues/12342 "Serious MGS Peace Walker bug fixed post-1.8.0"
[#11802]: https://github.com/hrydgard/ppsspp/issues/11802 "Windows: Detect Vulkan in separate process"
[#11744]: https://github.com/hrydgard/ppsspp/issues/11744 "VulkanDeviceAlloc: Memorytype per slab"
[#11747]: https://github.com/hrydgard/ppsspp/issues/11747 "Vk validation fixes, plus PowerVR swapchain hack"
[#11780]: https://github.com/hrydgard/ppsspp/issues/11780 "Naruto Shippuden Ultimate Ninja 3: Probably a better fix for the video hang issue."
[#11567]: https://github.com/hrydgard/ppsspp/issues/11567 "Correct shade mapping when light pos is all zeros"
[#11574]: https://github.com/hrydgard/ppsspp/issues/11574 "Correct various light param issues based on tests"
[#11577]: https://github.com/hrydgard/ppsspp/issues/11577 "Correct provoking vertex for lighting when flat shading"
[#11878]: https://github.com/hrydgard/ppsspp/issues/11878 "Sonic Rivals controls semi-broken"
[#11798]: https://github.com/hrydgard/ppsspp/issues/11798 "User report: Long notes in Rock Band Unplugged are not registered correctly"
[#11879]: https://github.com/hrydgard/ppsspp/issues/11879 "Fix apparent bug in #11094, fixes #11878 and likely #11798"
[#11172]: https://github.com/hrydgard/ppsspp/issues/11172 "handle cullface, help to #10597"
[#11813]: https://github.com/hrydgard/ppsspp/issues/11813 "Core: Fix lag sync on game start / after pause"
[#11425]: https://github.com/hrydgard/ppsspp/issues/11425 "[Refactoring] Improve spline/bezier."
[#11451]: https://github.com/hrydgard/ppsspp/issues/11451 "SDL/Vulkan window resize improvements"
[#11507]: https://github.com/hrydgard/ppsspp/issues/11507 "Improve support of Qt + USING_GLES2"
[#11537]: https://github.com/hrydgard/ppsspp/issues/11537 "CMake: Allow disabling Wayland support with USE_WAYLAND_WSI"
[#11541]: https://github.com/hrydgard/ppsspp/issues/11541 "CMake: Fix linking X11 when using EGL and not fbdev"
[#11632]: https://github.com/hrydgard/ppsspp/issues/11632 "SDL: Allow toggling fullscreen for GLES2 on desktops."
[#11746]: https://github.com/hrydgard/ppsspp/issues/11746 "CMakeLists: fix EGL detection for ARM devices"
[#11806]: https://github.com/hrydgard/ppsspp/issues/11806 "SDL: Force fullscreen desktop for USING_FBDEV"
[#11845]: https://github.com/hrydgard/ppsspp/issues/11845 "EGL: Avoid HDR mode. Uses unknownbrackets' changes from #11839."
[#11523]: https://github.com/hrydgard/ppsspp/issues/11523 "Add frameskip setting"
[#11530]: https://github.com/hrydgard/ppsspp/issues/11530 "Make DXT alpha and color calculation more accurate"
[#11618]: https://github.com/hrydgard/ppsspp/issues/11618 "Io: Ensure sign extension for error codes"
[#11531]: https://github.com/hrydgard/ppsspp/issues/11531 "WIP: Virtual readbacks"
[#11553]: https://github.com/hrydgard/ppsspp/issues/11553 "Remove constraint that virtual framebuffers have to represent VRAM."
[#11776]: https://github.com/hrydgard/ppsspp/issues/11776 "Only gate really expensive block transfers behind the setting."
[#11543]: https://github.com/hrydgard/ppsspp/issues/11543 "GPU: Dirty frag shader on depth write"
[#11620]: https://github.com/hrydgard/ppsspp/issues/11620 "Optimize out some stencil emulation, try to avoid depth write"
[#11584]: https://github.com/hrydgard/ppsspp/issues/11584 "D3D11: Allow shader blend to self"
[#11621]: https://github.com/hrydgard/ppsspp/issues/11621 "Vulkan: Avoid using Vulkan"
[#11658]: https://github.com/hrydgard/ppsspp/issues/11658 "Windows: Hide Vulkan/D3D11 if not available"
[#11665]: https://github.com/hrydgard/ppsspp/issues/11665 "Remove the \"Disable stencil test\" hack setting"
[#11666]: https://github.com/hrydgard/ppsspp/issues/11666 "Remove \"Timer Hack\" setting."
[#11667]: https://github.com/hrydgard/ppsspp/issues/11667 "Remove outdated TrueColor setting."
[#11689]: https://github.com/hrydgard/ppsspp/issues/11689 "Correct remote disc streaming with ipv6"
[#11700]: https://github.com/hrydgard/ppsspp/issues/11700 "http: Report errors reading discs"
[#11691]: https://github.com/hrydgard/ppsspp/issues/11691 "WIP: Vulkan/adreno: Apply workaround for Harvest Moon issue #10421"
[#11694]: https://github.com/hrydgard/ppsspp/issues/11694 "Vulkan: Limit stencil workaround to Adreno 5xx"
[#11720]: https://github.com/hrydgard/ppsspp/issues/11720 "Try to support Qt keyboard input directly. Fixes #11653"
[#11807]: https://github.com/hrydgard/ppsspp/issues/11807 "Qt: Re-enable Load button to browse for file"
[#11808]: https://github.com/hrydgard/ppsspp/issues/11808 "Qt: Correct text bind on first draw of string"
[#11740]: https://github.com/hrydgard/ppsspp/issues/11740 "Fix for weird Xbox B button behavior, see #10948."
[#11767]: https://github.com/hrydgard/ppsspp/issues/11767 "Compat: Force realistic UMD timing for F1 2006."
[#11785]: https://github.com/hrydgard/ppsspp/issues/11785 "GPU: Correct depth clamp range in range cull"
[#11859]: https://github.com/hrydgard/ppsspp/issues/11859 "GPU: Handle cull range properly drawing at offset"
[#11712]: https://github.com/hrydgard/ppsspp/issues/11712 "GLES: Detect Vivante GPU, disable vertex range culling"
[#11773]: https://github.com/hrydgard/ppsspp/issues/11773 "D3D9: Disable range culling on really old NVIDIA cards"
[#11787]: https://github.com/hrydgard/ppsspp/issues/11787 "GLES: Disable range culling on VideoCore/Vivante"
[#11812]: https://github.com/hrydgard/ppsspp/issues/11812 "Android: Allow using a custom Memory Stick storage path"
[#11822]: https://github.com/hrydgard/ppsspp/issues/11822 "interp: Correct vocp prefix handling"
[#11835]: https://github.com/hrydgard/ppsspp/issues/11835 "Correct vocp / vsocp prefix handling"
[#11831]: https://github.com/hrydgard/ppsspp/issues/11831 "Bundle libSDL inside app on macOS, fixes #11830"
[#11842]: https://github.com/hrydgard/ppsspp/issues/11842 "Win32 handle leak fix"
[#11508]: https://github.com/hrydgard/ppsspp/issues/11508 "Savedata: Write only one secure entry"
[#11441]: https://github.com/hrydgard/ppsspp/issues/11441 "Vulkan: Apply Themaister's patch, removing illegal pre-transitions of swapchain images. Fixes #11417 (crash on Note 9)"
[#11393]: https://github.com/hrydgard/ppsspp/issues/11393 "Implement vertex range culling"
[#10973]: https://github.com/hrydgard/ppsspp/issues/10973 "handle cull mode"
[#11182]: https://github.com/hrydgard/ppsspp/issues/11182 "GLES: Use accurate GLSL ver in postshader convert"
[#11438]: https://github.com/hrydgard/ppsspp/issues/11438 "Allow WASAPI device switching"
[#11461]: https://github.com/hrydgard/ppsspp/issues/11461 "Windows: Detect DirectInput devices after launch"
[#11307]: https://github.com/hrydgard/ppsspp/issues/11307 "Enable Discord integration for Mac and Linux."
[#10909]: https://github.com/hrydgard/ppsspp/issues/10909 "WebSocket based debugger interface"
[#11447]: https://github.com/hrydgard/ppsspp/issues/11447 "Avoid calling any GL calls during shutdown on Android. Should help #11063"
[#11350]: https://github.com/hrydgard/ppsspp/issues/11350 "TexCache: Optimize DXT3/DXT5 decode to single pass"