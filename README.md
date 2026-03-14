PPSSPP - a fast and portable PSP emulator
=========================================

Created by Henrik Rydgård

Additional code by many contributors, see the Credits screen

Originally released under the GPL 2.0 (and later) in November 2012

Official website: https://www.ppsspp.org/

Discord: https://discord.gg/5NJB6dD

No BIOS file required to play, PPSSPP is an "HLE" emulator.  Default settings balance good compatibility and speed.

To contribute, see [the development page](https://www.ppsspp.org/docs/development/).  Help testing, investigating, or fixing is always welcome.  See [the list of issues](https://github.com/hrydgard/ppsspp/issues).

For the latest source code, see [our GitHub page](https://github.com/hrydgard/ppsspp).

For documentation of all kinds (usage, reference, development), see the [documentation on the main website](https://www.ppsspp.org/docs)

For build instructions and other development tutorials, see [the wiki](https://github.com/hrydgard/ppsspp/wiki).

To download fresh development builds for Android, Windows and Mac, [go to the /devbuilds page](https://www.ppsspp.org/devbuilds)

For game compatibility, see [community compatibility feedback](https://report.ppsspp.org/games).

What's new in 1.20.3
--------------------

- Fix issue preventing ad hoc relay servers from working when RetroAchievements are enabled on Windows ([#21420])
- Fix crash/failure setting the background on iOS ([#21409])
- Fix logging to file ([#21412])
- Networking settings cleanup ([#21418])
- Some fixes for assorted rare crashes ([#21422])
- Fix issues when unpausing using a controller binding ([#21424]), DualSense Edge detection on Windows ([#21426])
- Fix missing savestate undo button ([#21425])
- MMPX texture upscaling algorithm has been restored, the new one has been improved ([#21376]) and renamed MMPX Advanced ([#21421])

What's new in 1.20.2
--------------------

- Improved server list for ad hoc multiplayer, dynamically updated and you can now add/remove entries ([#21326], [#21389], [#21351])
- Fix broken multitouch on iOS with OpenGL ([#21350])
- Ad hoc relay connection improvements ([#21352])
- Fix a lot of minor UI issues ([#21400], [#21362])
- Fix background image selection on Android ~~and iOS~~ ([#21345], [#21384], [#21371])
- Fix file permission issue on iOS ([#21374])
- Add a "hold" version of axis swap toggle ([#21357])
- Fix regression in Gripshift ([#21377])
- Fix crash on audio device switch on Windows ([#21341])
- Fix timing glitches in gamepad input on Windows ([#21393])
- And other assorted fixes.

What's new in 1.20
------------------

- User interface
  - New feature: Proper support for Portrait Mode UI - you can now rotate your phone to vertical mode, and things will mostly work fine! ([#21185], [#21044]...)
  - New feature: Separate touch controls and display layout configs for landscape/portrait, new default touch control layout for portrait mode
  - New feature: Upload files via HTTP from devices on the same network
  - Savestate count limit can now be configured ([#21112])
  - UI icons are now vector-based, making them look sharper on high-DPI screens ([#20824], [#20870])
  - Better handling of unsupported ISOs ([#21169], [#21166])
  - Misc: Improved text rendering, chat box improvements ([#20653]), PIC0 is now visible on the game screen ([#20686]), ([#20611]), rotation fixes ([#21145]), input fixes ([#21248]), fixes for XBox FullScreen Experience, background stretch ([#21211]), handle savedata in main game browser ([#21277]), Discord Presence improvement ([#21246])
  - Show ICON1.PMF on the game info screen ([#21303])

- Regression fixes
  - In 7th Dragon, voice-related crashes are fixed ([#20837])
  - Soundtrack is working again in Tony Hawk's Project 8 ([#20775])
  - Fix crash in StormBasic homebrew apps ([#20715])
  - Built-in songs in Beats work again ([#20662])
   - Fix adhoc in Syphon Filter games ([#20642])
  - Fix strange cutscene speedups in Power Stone 2 (Power Stone Collection) ([#2124])
  - Work around game bug to help more custom songs play again in GTA LCS ([#20692])

- Multiplayer
  - Support aemu relay servers for AdHoc multiplayer ([#21116], [#21271])

- Controller and touch screen input
  - Native support for DualSense ([#20580], [#20620], [#21191]), DualShock and Switch Pro ([#20647]) controllers on Windows - including tilt controls for DualSense and Switch Pro
  - Misc touch screen and controller mapping improvements ([#21195], [#21197], [#21258], [#21325])
  - Add simple virtual keyboard for text input for platforms that don't have one ([#21306])

- Graphics
  - Fixed multiple graphical effects in Tales of Phantasia X
  - Reworked the VSync setting to be more intuitive. It's now pretty much never a bad idea to turn it on.
  - Fixes for various glitches in Tales of Phantasia X ([#21173], [#21141], [#21205])
  - Some more framebuffer readback workarounds ([#20640], [#20631], [#20632])
  - MMPX upscaling enhancements by crashGG ([#20622], [#20541])
  - Huge rendering speedup in Brave Story ([#21151])
  - Fix rendering bug caused by CPU emulation bug in ATV Offroad Fury: Blazing Trails ([#21238])
  - Partially fix rendering problem in Mahjong Artifacts ([#21244]) and The Mystery of the Crystal Portal ([#21236])
  - Boost the GPU in Outrun 2006 to avoid unnecessary slowdowns ([#21304])

- Other game fixes
  - Fix savedata problem in Silent Hill: Shattered Memories ([#21294])

- Audio
  - Added "Smooth" playback mode, which evens out glitches (while adding a very small amount of latency)
  - Remove DirectSound support ([#20533]), add support for ultra-low-latency streams on WASAPI, available on some devices ([#20535])

- Platform support
  - Windows
    - D3D9 support has been removed ([#19951], [#20490])
    - ARM64 is now officially supported, and works great ([#20863])
    - Correct fullscreen mode in Xbox Fullscreen Experience ([#21189])
    - Assorted improvements ([#20778], [#20774])
  - Android
    - The minimum Android version has been raised for technical reasons, KitKat is no longer supported ([#19658])
    - Support for Android devices with 16KB page size. This is a technical change, required by Google Play, that unfortunately forced us to drop compatibility with the very oldest devices (Android < 5). ([#20788])
    - Shortcut widgets can now be created even if the app isn't running ([#20798])
    - Removed support for classic Moga controllers, these were only a thing on early Android ([#20762])
  - iOS
    - Add support for screen rotation
  - Linux
    - Loongarch improvements by KatyushaScarlet ([#20683], [#20644], [#20599], [#20594]), text rendering improvements ([#21163])
    - SDL fullscreen problems fixed ([#21300], more)
  - UWP
    - Migrate code base to C++/WinRT ([#21100])

- Debugger
  - ImDebugger improvements ([#20861], [#20779], [#20657], [#20637], [#20550], [#20523])
  - Websocket debugger fixes ([#20749])

- Other
  - RetroAchievements: Upgrade the support library with perf fixes ([#21081]), various fixes and improvements
  - Add workaround for infamous God of War crash ([#21148])
  - Fix assorted minor UI bugs ([#21042], ...)

What's new in 1.19.3
--------------------

- Fixed crash on startup in Tony Hawk's Underground 2 ([#20573])
- Fixed crash/hang when accessing the menu in FFII if readbacks were configured to copy-to-tex ([#20573])
- Fix issue with detection of some types of saving, for the save reminders on exit ([#20623])
- Fixed music and other audio in modded games (various football games, Crazy Taxi w/ original soundtrack, etc) ([#20566], [#20571])
- Fix exiting not functioning correctly on Windows in some circumstances ([#20607])
- Minor UI and key binding fixes ([#20604])
- Fix crash/blackscreen when switching from skip buffer effects to auto-frameskip rendering ([#20605])
- Fix bug in Mac/Linux builds where the mouse got hidden and stuck if you enabled mouse input mapping ([#20612])
- Fix black screen on save/load in the Football Manager Handheld games ([#20616])
- Translation improvements

What's new in 1.19.2
--------------------

- In-game save/load not properly detected for saving reminder ([#20500])
- Install savedata from ZIP was partially broken ([#20498])
- Fixed module loader bug affecting a few games ([#20513])
- Fixed some hangs in the Windows debugger ([#20510])
- Fix performance problem in texture replacement ([#20520])
- Assorted fixes ([#20518], [#20514], [#20502], [#20515])

What's new in 1.19.1
--------------------

- Fix selecting background image on Android ([#20477])
- Fix RetroAchievements regression for multi-executable games ([#20469])
- Possible fix for Mac audio device selection issue ([#20482])
- Add workaround for Dragon's Lair not working with LLE scePsmf ([#20468])
- Prevent trying to load obviously-corrupt CSO/CHD files ([#20466])
- Fix regression for homebrew apps that request extra memory ([#20457])
- Fix grid drawing in the various Robot Taisen games ([#20456])
- Fix crash in UI when viewing a directory with multiple NPDRM ISOs ([#20453])

What's new in 1.19
------------------

- Audio
	- Brand new sceAtrac implementation, fixing Flatout and multiple other long-standing issues! ([#20098], [#20136], [#20138], [#20162])
	- More correctly implemented sceAtrac-through-sceSas functionality, fixing voice in Sol Trigger and multiple minis ([#20156])
	- Better support for using sceAudiocodec directly, fixing music in some homebrew apps ([#20199], [#20246], [#20253], [#20209])
	- Volume control improvement ([#19969], [#19971])

- Rendering fixes and performance
    - Important: This will be the last major version with D3D9 support. D3D11 will of course continue to be supported, along with Vulkan and OpenGL.
	- Fix smoke effects in Jak'n'Daxter by re-using the fix from Ratchet & Clank ([#20032])
    - Software depth buffer rendering added to fix lens flares in multiple games efficiently (Midnight Club LA, Wipeout, Syphon Filter etc) ([#19748], [#19764], [#20231], [#19955], etc)
	- Multiple rendering-related crash fixes ([#20270], [#20346], [#20344])
	- Texture replacement load speed corrected and can now be controlled ([#20286]), regression fix for zipped packs ([#19860])
	- Additional assorted compatibility and other fixes ([#20376], [#20070], [#19685])
	- Persona 1/2 readback fixes ([#20067], [#20068])
	- Other correctness fixes ([#20233], [#20255], [#19686])

- General emulation
	- Allow disabling HLE on a per-module basis (if games ship the module so we can load it). Enabled for sceCcc ([#20218]), and more importantly scePsmf ([#20208]), fixing multiple issues.
	- Additional module loading improvements ([#20114])
	- More precise sleep function on Windows ([#20054])
	- More replacement texture improvements, allow replacing game icons ([#19975], [#19978])
	- Disable certain features like fast-forward when networking is on, to avoid de-syncs ([#20311])
	- Fixes mono Atrac3 as video soundtrack ([#19800])
	- Improvements to RISC-V support ([#20352])

- Control and input
	- Touch gliding support added ([#19983])
	- Allow simultaneous DInput and XInput input ([#20347])

- New "Dear ImGui"-based PSP debugger: ImDebugger
	- Allows very rapid development of debugging features as-needed, and also implements a pretty good Ge debugger for stepping through draws.
	- Unlike the old Win32 debugger (but similar to the unmaintained web debugger), works on all platforms (though cumbersome on touchscreens)
	- Major commits (though there are many more): ([#20242], [#20240], [#20294])

- Multiplayer
	- Added Infrastructure multiplayer support, with automatic DNS configuration ([#19865], [#19869], [#20221], [#20220], [#19875])
	    - Note: Only games that people have made "revival servers" for will work. See [here](https://www.ppsspp.org/docs/multiplayer/infrastructure-servers/).
	- Implement more of sceNet (prerequisite for the former feature).
	- Socket re-mapping implemented ([#19827])
	- Assorted bugfixes, thanks ANR2ME ([#20247], [#20245], [#19843], [#19849], [#19836])

- UI
	- Loading spinner now actually spins properly, the app no longer appears to hang ([#20341])
	- Minor features: Asks for confirmation on exit in most scenarios ([#19996], [#20023]), DPI scaling ([#20013]), can pause without menu ([#19883])
	- As usual a lot of tweaks, perf fixes, and fixes for hangs and crashes ([#20343], [#20332], [#20305], [#20303], [#20299], [#20163], [#20152], [#20143], [#20079], [#20137], [#20374])
	- Two new color themes ([#20334], [#20335]), related themability fixes ([#19984], [#19995], [#20308])
	- Improvements and bug fixes in the savedata manager ([#19771], [#20170])
	- Add "Move to trash" deletion funcionality to multiple platforms ([#20230], [#20261])
	- Add ability to take "raw" screenshots of gameplay ([#20029])
	- More files can be loaded directly from ZIP ([#20243])
	- Developer Settings are now tabbed for easier access ([#20228])
	- Switch to the full libpng API so we can disable gamma correction, like the real PSP ([#20033])
	- Support displaying the battery percentage on more platforms ([#19973], [#19967])
	- Allow picking a background image on iOS ([#20370])

- Platform compatibility
	- Exclude older Macs from using Vulkan (too many black screens, hangs) ([#20236])
	- Use portable-file-dialogs to provide file-open dialogs on Linux ([#20175])
	- "Cache full ISO in RAM" is now correctly hidden where it doesn't work ([#20165])
	- Now rendering at proper resolution on newer Macs ([#20011])
	- Mouse input improved on Android, including separate button mapping ([#19915])
	- Use the correct font again on Mac/iOS ([#19874])
	- Multiple file access optimizations made to make the most out of the flawed foundation called Android Scoped Storage ([#19668])

What's new in earlier versions
------------------------------
See [history.md](history.md).

[comment]: # (LINK_LIST_BEGIN_HERE)
[#20098]: https://github.com/hrydgard/ppsspp/issues/20098 "New implementation of sceAtrac (the Atrac3+ module)"
[#20136]: https://github.com/hrydgard/ppsspp/issues/20136 "New sceAtrac impl: Fix low level decoding"
[#20138]: https://github.com/hrydgard/ppsspp/issues/20138 "Use the new sceAtrac implementation"
[#20162]: https://github.com/hrydgard/ppsspp/issues/20162 "at3_standalone: Make all allocations aligned."
[#20156]: https://github.com/hrydgard/ppsspp/issues/20156 "Reimplement Atrac-through-SAS"
[#20199]: https://github.com/hrydgard/ppsspp/issues/20199 "Partially implement sceAudiocodec"
[#20246]: https://github.com/hrydgard/ppsspp/issues/20246 "sceAudiocodec: Restore AAC support, add AT3 (non-plus) support"
[#20253]: https://github.com/hrydgard/ppsspp/issues/20253 "Revert to using FFMPEG for MP3 playback"
[#20209]: https://github.com/hrydgard/ppsspp/issues/20209 "More HLE cleanup, fix MP3 in sceAudiocodec"
[#19969]: https://github.com/hrydgard/ppsspp/issues/19969 "Volume control UI changes, part 1"
[#19971]: https://github.com/hrydgard/ppsspp/issues/19971 "Volume control UI changes, part 2"
[#20032]: https://github.com/hrydgard/ppsspp/issues/20032 "Fix Jak & daxter smoke effects (same problems as Ratchet)"
[#19748]: https://github.com/hrydgard/ppsspp/issues/19748 "Render a software depth buffer in parallel with HW rendering"
[#19764]: https://github.com/hrydgard/ppsspp/issues/19764 "Enable depth raster in all backends"
[#20231]: https://github.com/hrydgard/ppsspp/issues/20231 "Fix lens flare in L.A. Rush"
[#19955]: https://github.com/hrydgard/ppsspp/issues/19955 "CrossSIMD: Add a simple unit test, fix a couple of operations in the no-simd path"
[#20270]: https://github.com/hrydgard/ppsspp/issues/20270 "Avoid using shader blending in skip-buffer-effects mode"
[#20346]: https://github.com/hrydgard/ppsspp/issues/20346 "Metal Gear Acid 2 oil spill crashfix"
[#20344]: https://github.com/hrydgard/ppsspp/issues/20344 "Fix crash in texture saving, fix Mega Minis 2"
[#20286]: https://github.com/hrydgard/ppsspp/issues/20286 "New setting: Texture replacement load speed"
[#19860]: https://github.com/hrydgard/ppsspp/issues/19860 "Fix regression loading zipped texture packs"
[#20376]: https://github.com/hrydgard/ppsspp/issues/20376 "Vulkan semaphore fix"
[#20070]: https://github.com/hrydgard/ppsspp/issues/20070 "Software renderer: Fix regression with gouraud shaded lines"
[#19685]: https://github.com/hrydgard/ppsspp/issues/19685 "Cull through-mode 2D draws against scissor rectangle"
[#20067]: https://github.com/hrydgard/ppsspp/issues/20067 "Hook framebuffer readback function in Persona 1."
[#20068]: https://github.com/hrydgard/ppsspp/issues/20068 "Hook framebuffer readback function in Persona 2"
[#20233]: https://github.com/hrydgard/ppsspp/issues/20233 "Fix Star Ocean with MSAA enabled: don't use the blit optimization (Vulkan)"
[#20255]: https://github.com/hrydgard/ppsspp/issues/20255 "Vulkan: Auto Max Quality: Avoid conflict between aniso filtering and nearest filtering"
[#19686]: https://github.com/hrydgard/ppsspp/issues/19686 "Fix small accuracy issue in through-mode 2D culling"
[#20218]: https://github.com/hrydgard/ppsspp/issues/20218 "Misc fixes and cleanup, use DisableHLE with \"sceCcc\""
[#20208]: https://github.com/hrydgard/ppsspp/issues/20208 "Disable HLE of scePsmf and scePsmfPlayer"
[#20114]: https://github.com/hrydgard/ppsspp/issues/20114 "Fix sceUtilityLoadModuleAv, allow browsing memory tags in the memory viewer"
[#20054]: https://github.com/hrydgard/ppsspp/issues/20054 "Switch to sleep_precise for WaitUntil(), bump VMA and gradle versions"
[#19975]: https://github.com/hrydgard/ppsspp/issues/19975 "Allow custom game icons if texture replacement is enabled"
[#19978]: https://github.com/hrydgard/ppsspp/issues/19978 "ZipFileReader: Small performance optimization when reading"
[#19800]: https://github.com/hrydgard/ppsspp/issues/19800 "Fix playback of mono Atrac3+ tracks in videos"
[#20352]: https://github.com/hrydgard/ppsspp/issues/20352 "Fix RiscVEmitter::QuickFLI (#20351)"
[#19983]: https://github.com/hrydgard/ppsspp/issues/19983 "Touch: Implement \"Touch gliding\" (keep all dragged/touched buttons pressed until touch release)"
[#20347]: https://github.com/hrydgard/ppsspp/issues/20347 "DInput: Properly ignore XInput devices individually, instead of ignoring all if XInput is available"
[#20242]: https://github.com/hrydgard/ppsspp/issues/20242 "ImDebugger: Add some audio investigation tools"
[#20240]: https://github.com/hrydgard/ppsspp/issues/20240 "minor-breakpoint-improvements"
[#20294]: https://github.com/hrydgard/ppsspp/issues/20294 "Misc ImDebugger improvements"
[#19865]: https://github.com/hrydgard/ppsspp/issues/19865 "Infrastructure Auto DNS: Preconfigured per-game infrastructure DNS through JSON"
[#19869]: https://github.com/hrydgard/ppsspp/issues/19869 "DNS autoconf: Fix games that do their own DNS queries"
[#20221]: https://github.com/hrydgard/ppsspp/issues/20221 "More infrastructure networking fixes"
[#20220]: https://github.com/hrydgard/ppsspp/issues/20220 "Infrastructure multiplayer fixes"
[#19875]: https://github.com/hrydgard/ppsspp/issues/19875 "Online: Fix DNS server default, show revival team credits on pause screen"
[#19827]: https://github.com/hrydgard/ppsspp/issues/19827 "sceNetInet socket remap"
[#20247]: https://github.com/hrydgard/ppsspp/issues/20247 "[Adhoc] Fixed truncated adhoc group name issue."
[#20245]: https://github.com/hrydgard/ppsspp/issues/20245 "[Adhoc] Partially fixes multiplayer regression on GTA games."
[#19843]: https://github.com/hrydgard/ppsspp/issues/19843 "An attempt to fix Driver 76 multiplayer"
[#19849]: https://github.com/hrydgard/ppsspp/issues/19849 "Fix bug in sceNetInetPoll, similar to the previous select bug"
[#19836]: https://github.com/hrydgard/ppsspp/issues/19836 "An attempt to fix UNO single player."
[#20341]: https://github.com/hrydgard/ppsspp/issues/20341 "More async GPU init"
[#19996]: https://github.com/hrydgard/ppsspp/issues/19996 "Add confirmation on exit"
[#20023]: https://github.com/hrydgard/ppsspp/issues/20023 "Add confirmation popup support on Exit App key, libretro buildfix"
[#20013]: https://github.com/hrydgard/ppsspp/issues/20013 "UI DPI scale setting"
[#19883]: https://github.com/hrydgard/ppsspp/issues/19883 "Add new mappable key to pause without the pause menu."
[#20343]: https://github.com/hrydgard/ppsspp/issues/20343 "Avoid getting stuck in a loop when using auto-load-state and the state is bad"
[#20332]: https://github.com/hrydgard/ppsspp/issues/20332 "Fix reset bug, frame advance bug, translation issues"
[#20305]: https://github.com/hrydgard/ppsspp/issues/20305 "Fix exiting from framedump playback, some std::thread code cleanup"
[#20303]: https://github.com/hrydgard/ppsspp/issues/20303 "Some UI fixes, crashfixes"
[#20299]: https://github.com/hrydgard/ppsspp/issues/20299 "Don't call rc_client_do_frame when paused."
[#20163]: https://github.com/hrydgard/ppsspp/issues/20163 "Switch the recent files manager to the \"command processor on thread\" pattern, to avoid blocking the main thread"
[#20152]: https://github.com/hrydgard/ppsspp/issues/20152 "Settings: Load tabs on demand, instead of all at once"
[#20143]: https://github.com/hrydgard/ppsspp/issues/20143 "Fix crash when saving screenshots on a thread"
[#20079]: https://github.com/hrydgard/ppsspp/issues/20079 "Touch control layout editor: Resize the game image to fit the editing surface"
[#20137]: https://github.com/hrydgard/ppsspp/issues/20137 "Screenshot performance improvement"
[#20374]: https://github.com/hrydgard/ppsspp/issues/20374 "More crashfixes"
[#20334]: https://github.com/hrydgard/ppsspp/issues/20334 "Add Alpine theme"
[#20335]: https://github.com/hrydgard/ppsspp/issues/20335 "Add Strawberry theme"
[#19984]: https://github.com/hrydgard/ppsspp/issues/19984 "Theme system fixes and additions"
[#19995]: https://github.com/hrydgard/ppsspp/issues/19995 "More theming work"
[#20308]: https://github.com/hrydgard/ppsspp/issues/20308 "Make slider colors themable"
[#19771]: https://github.com/hrydgard/ppsspp/issues/19771 "UI fixes: Rework savedata manager a bit, default keyboard focus to Cancel in confirmation dialogs"
[#20170]: https://github.com/hrydgard/ppsspp/issues/20170 "Fix bugs in savedata manager"
[#20230]: https://github.com/hrydgard/ppsspp/issues/20230 "Move to trash instead of deleting important files like savedata (Windows only so far)"
[#20261]: https://github.com/hrydgard/ppsspp/issues/20261 "Trash handling is too high level for FileUtil, move it up."
[#20029]: https://github.com/hrydgard/ppsspp/issues/20029 "Add long-requested feature to take screenshots of the raw game image instead of the processed output."
[#20243]: https://github.com/hrydgard/ppsspp/issues/20243 "Add a ZipFileLoader, which can let us load any single-file file type from a standard zip file"
[#20033]: https://github.com/hrydgard/ppsspp/issues/20033 "Use libpng's full API so we can ignore gamma. Fixes Driver '76's icon."
[#20228]: https://github.com/hrydgard/ppsspp/issues/20228 "Developer tools screen: Use tabs"
[#19973]: https://github.com/hrydgard/ppsspp/issues/19973 "Add support for displaying the battery percentage on Windows."
[#19967]: https://github.com/hrydgard/ppsspp/issues/19967 "Support battery percentage display on SDL"
[#20370]: https://github.com/hrydgard/ppsspp/issues/20370 "iOS: Implement a background image picker"
[#20236]: https://github.com/hrydgard/ppsspp/issues/20236 "Blacklist older Intel GPUs from using Vulkan on Mac"
[#20175]: https://github.com/hrydgard/ppsspp/issues/20175 "Add Linux file dialog support through \"portable-file-dialogs\""
[#20165]: https://github.com/hrydgard/ppsspp/issues/20165 "Reintroduce and fix feature checks for \"Cache full ISO in RAM\""
[#20011]: https://github.com/hrydgard/ppsspp/issues/20011 "macOS SDL: Set the metal layer resolution properly, remove DPI hacks."
[#19915]: https://github.com/hrydgard/ppsspp/issues/19915 "Android: Improve mouse input"
[#19874]: https://github.com/hrydgard/ppsspp/issues/19874 "macOS/iOS: register font with CoreText"
[#19668]: https://github.com/hrydgard/ppsspp/issues/19668 "File system perf part 1: Remove some unnecessary file access"
[#20311]: https://github.com/hrydgard/ppsspp/issues/20311 "Automatically disable fast forward feature when doing multiplayer."
[#20477]: https://github.com/hrydgard/ppsspp/issues/20477 "Fix background image selection on Android"
[#20469]: https://github.com/hrydgard/ppsspp/issues/20469 "Fix RetroAchievements for multi-exe games, add some sanity checks"
[#20482]: https://github.com/hrydgard/ppsspp/issues/20482 "SDL audio: Improve logging, don't auto-switch device during the first 3 seconds of execution"
[#20468]: https://github.com/hrydgard/ppsspp/issues/20468 "Add workaround for scePsmf LLE not working with Dragon's Lair"
[#20466]: https://github.com/hrydgard/ppsspp/issues/20466 "ISO loading: Check CSO and CHD files \"early\""
[#20457]: https://github.com/hrydgard/ppsspp/issues/20457 "Fix homebrew apps that request large memory"
[#20456]: https://github.com/hrydgard/ppsspp/issues/20456 "Fix grid drawing in Robot Taisen games."
[#20453]: https://github.com/hrydgard/ppsspp/issues/20453 "libkirk concurrency fixes"
[#20500]: https://github.com/hrydgard/ppsspp/issues/20500 "Correct save/load detection for the reminder."
[#20498]: https://github.com/hrydgard/ppsspp/issues/20498 "Install savedata from zip seems to have regressed"
[#20513]: https://github.com/hrydgard/ppsspp/issues/20513 "ELF loader: Revert bad export check"
[#20510]: https://github.com/hrydgard/ppsspp/issues/20510 "Fix bug in Win32 debugger, misc fixes"
[#20520]: https://github.com/hrydgard/ppsspp/issues/20520 "Replacement textures: Don't spend frame time waiting for a texture to be finished"
[#20518]: https://github.com/hrydgard/ppsspp/issues/20518 "Disable the Cache full ISO in RAM feature on 32-bit builds"
[#20514]: https://github.com/hrydgard/ppsspp/issues/20514 "Developer tools: Add UI to force-enable HLE of modules that we have disabled HLE of"
[#20502]: https://github.com/hrydgard/ppsspp/issues/20502 "Switching from IR Interpreter to JIT in-game crashes"
[#20515]: https://github.com/hrydgard/ppsspp/issues/20515 "Additional fixes for 1.19.2"
[#20573]: https://github.com/hrydgard/ppsspp/issues/20573 "Fix crash on second logo in Tony Hawk's Underground 2"
[#20566]: https://github.com/hrydgard/ppsspp/issues/20566 "Atrac code cleanup, logging and comment fixes"
[#20571]: https://github.com/hrydgard/ppsspp/issues/20571 "More Atrac3 refactor"
[#20607]: https://github.com/hrydgard/ppsspp/issues/20607 "Fix issue where PPSSPP wouldn't correctly quit if you closed it with the corner X button in-game"
[#20604]: https://github.com/hrydgard/ppsspp/issues/20604 "Fix some small UI regressions"
[#20605]: https://github.com/hrydgard/ppsspp/issues/20605 "Fix strange edge case when switching to auto-frameskip from non-buffered rendering"
[#20612]: https://github.com/hrydgard/ppsspp/issues/20612 "SDL: Fix bug where the mouse got stuck in relative mode when mapping mouse inputs"
[#20616]: https://github.com/hrydgard/ppsspp/issues/20616 "Football Manager Handheld series: Fix blackscreen"
[#20623]: https://github.com/hrydgard/ppsspp/issues/20623 "Fix tracking for the savedata reminder for several invisible save types"
[#21185]: https://github.com/hrydgard/ppsspp/issues/21185 "Move controls up a bit in portrait mode, render fix of main screen buttons when held"
[#21044]: https://github.com/hrydgard/ppsspp/issues/21044 "Add convenient UI on the pause screen for changing/locking the screen orientation"
[#21112]: https://github.com/hrydgard/ppsspp/issues/21112 "Rework listing of savestates - a single scan operation instead of lots of File::Exists and GetFileInfo calls."
[#20824]: https://github.com/hrydgard/ppsspp/issues/20824 "Load UI atlas images from SVG"
[#20870]: https://github.com/hrydgard/ppsspp/issues/20870 "UI: Convert rendering to use premultiplied alpha"
[#21169]: https://github.com/hrydgard/ppsspp/issues/21169 "Make UMD_VIDEO discs with game data detect as games."
[#21166]: https://github.com/hrydgard/ppsspp/issues/21166 "Improve handling of the UMD VIDEO error case (no, we still don't support them)"
[#20653]: https://github.com/hrydgard/ppsspp/issues/20653 "Fix assorted interaction problems with the chat menu"
[#20686]: https://github.com/hrydgard/ppsspp/issues/20686 "PIC0 support"
[#20611]: https://github.com/hrydgard/ppsspp/issues/20611 "Show a tiny indicator in the top left of the screen when the game is saving or loading"
[#21145]: https://github.com/hrydgard/ppsspp/issues/21145 "Correct functionality of the display rotation control rotation."
[#21248]: https://github.com/hrydgard/ppsspp/issues/21248 "Control mapper refactor, allow unpausing using analog triggers if mapped"
[#21211]: https://github.com/hrydgard/ppsspp/issues/21211 "Background stretch: Only resort to crop to avoid extreme squishing of the image."
[#21277]: https://github.com/hrydgard/ppsspp/issues/21277 "Fix a rare savedata crash, handle savedata files better in the game browser"
[#21246]: https://github.com/hrydgard/ppsspp/issues/21246 "Reflect Gold status in Discord Rich Presence"
[#21303]: https://github.com/hrydgard/ppsspp/issues/21303 "Show ICON1.PMF videos on game info screen"
[#20837]: https://github.com/hrydgard/ppsspp/issues/20837 "Fix a load slowdown, possible fix for voices in 7th Dragon II"
[#20775]: https://github.com/hrydgard/ppsspp/issues/20775 "Tony Hawk's Project 8 Soundtrack features disabled after update"
[#20715]: https://github.com/hrydgard/ppsspp/issues/20715 "Fix typo in mono atrac initialization. Fixes crash in StormBasic homebrews"
[#20662]: https://github.com/hrydgard/ppsspp/issues/20662 "Atrac3+: Fix parsing for the AA3 file format"
[#20642]: https://github.com/hrydgard/ppsspp/issues/20642 "Disable the memcpy slicing thing (for HLE memcpys) in Syphon Filter games"
[#2124]: https://github.com/hrydgard/ppsspp/issues/2124 "Fixed a build error on non-win32 platfroms"
[#20692]: https://github.com/hrydgard/ppsspp/issues/20692 "GTA Liberty City Stories no longer plays custom AT3 music"
[#21116]: https://github.com/hrydgard/ppsspp/issues/21116 "Support adhoc relay from aemu to ppsspp"
[#21271]: https://github.com/hrydgard/ppsspp/issues/21271 "Fix FlatOut Head On in aemu_postoffice relay mode"
[#20580]: https://github.com/hrydgard/ppsspp/issues/20580 "Windows: Add basic native support for DualShock / DualSense"
[#20620]: https://github.com/hrydgard/ppsspp/issues/20620 "Control the LEDs on DualShock/DualSense gamepads"
[#21191]: https://github.com/hrydgard/ppsspp/issues/21191 "DualSense on Windows: Support Bluetooth control messages"
[#20647]: https://github.com/hrydgard/ppsspp/issues/20647 "Windows: Add basic native support for Switch Pro controllers"
[#21195]: https://github.com/hrydgard/ppsspp/issues/21195 "Touch controls: Don't fade out while buttons are being held"
[#21197]: https://github.com/hrydgard/ppsspp/issues/21197 "Split gesture control into left and right zones"
[#21258]: https://github.com/hrydgard/ppsspp/issues/21258 "Fix Driver 76 not creating adhoc sockets"
[#21173]: https://github.com/hrydgard/ppsspp/issues/21173 "Add workaround for Tales of Phantasia X flicker problem"
[#21141]: https://github.com/hrydgard/ppsspp/issues/21141 "Fix problem with block image copies within a framebuffer in Vulkan, misc fixes"
[#21205]: https://github.com/hrydgard/ppsspp/issues/21205 "Fix issue with destination rectangles with image block copies"
[#20640]: https://github.com/hrydgard/ppsspp/issues/20640 "Hook FB Readbacks in games"
[#20631]: https://github.com/hrydgard/ppsspp/issues/20631 "Hook framebuffer readback function in Never7, Ever17, and Remember11"
[#20632]: https://github.com/hrydgard/ppsspp/issues/20632 "Hook framebuffer readback function in Steins;Gate"
[#20622]: https://github.com/hrydgard/ppsspp/issues/20622 "enhance MMPX algorithm final part"
[#20541]: https://github.com/hrydgard/ppsspp/issues/20541 "enhance MMPX algorithm part 2"
[#21151]: https://github.com/hrydgard/ppsspp/issues/21151 "Brave Story: Hack to make the bloom effect run much more efficiently"
[#21238]: https://github.com/hrydgard/ppsspp/issues/21238 "Fix rare ARM64 JIT bug"
[#21244]: https://github.com/hrydgard/ppsspp/issues/21244 "Fix black background in Mahjong Artifacts, minor GE debugger improvement"
[#21236]: https://github.com/hrydgard/ppsspp/issues/21236 "Texturing fix, initial window size fix, etc"
[#21304]: https://github.com/hrydgard/ppsspp/issues/21304 "UI fixes, add GPU \"overclock\" for Outrun"
[#21294]: https://github.com/hrydgard/ppsspp/issues/21294 "Add compat flag to freeze reported file creation times. Helps Silent Hill: Shattered Memories"
[#20533]: https://github.com/hrydgard/ppsspp/issues/20533 "Windows: Remove DirectSound support"
[#20535]: https://github.com/hrydgard/ppsspp/issues/20535 "Add the new low-latency WASAPI backend, add audio device selection on Windows"
[#19951]: https://github.com/hrydgard/ppsspp/issues/19951 "Remove D3D9 support, to make future changes easier"
[#20490]: https://github.com/hrydgard/ppsspp/issues/20490 "Remove some D3D9 leftovers"
[#20863]: https://github.com/hrydgard/ppsspp/issues/20863 "Arm64 FP controlword support on Windows, plus TR fix"
[#21189]: https://github.com/hrydgard/ppsspp/issues/21189 "Rework fullscreen on Windows based on tests"
[#20778]: https://github.com/hrydgard/ppsspp/issues/20778 "Windows: More startup performance"
[#20774]: https://github.com/hrydgard/ppsspp/issues/20774 "Windows: Speed up startup"
[#19658]: https://github.com/hrydgard/ppsspp/issues/19658 "Add support for 16kb page size on Android"
[#20788]: https://github.com/hrydgard/ppsspp/issues/20788 "Android: Update to NDK 28"
[#20798]: https://github.com/hrydgard/ppsspp/issues/20798 "Android shortcuts: Fix so that setting icons work even if PPSSPP isn't running"
[#20762]: https://github.com/hrydgard/ppsspp/issues/20762 "Android: Remove support for Moga controllers"
[#20683]: https://github.com/hrydgard/ppsspp/issues/20683 "loongarch: Fix various IR JIT & VertexJIT bugs"
[#20644]: https://github.com/hrydgard/ppsspp/issues/20644 "loongarch: Implement Morph in VertexJIT & QuickTexHashLSX"
[#20599]: https://github.com/hrydgard/ppsspp/issues/20599 "Detect and enable LSX/LASX on LoongArch based on compiler predefined macros."
[#20594]: https://github.com/hrydgard/ppsspp/issues/20594 "Disable LASX on LoongArch64"
[#21163]: https://github.com/hrydgard/ppsspp/issues/21163 "SDL text drawer: Fix memory leak, add additional checks"
[#21300]: https://github.com/hrydgard/ppsspp/issues/21300 "Fix fullscreen bugs in SDL port."
[#20861]: https://github.com/hrydgard/ppsspp/issues/20861 "ImDebugger: Add a JIT viewer window"
[#20779]: https://github.com/hrydgard/ppsspp/issues/20779 "Add simple ParamSFO viewer to ImDebugger"
[#20657]: https://github.com/hrydgard/ppsspp/issues/20657 "Default to Vulkan on earlier Windows versions, show sceAac contexts in debugger"
[#20637]: https://github.com/hrydgard/ppsspp/issues/20637 "Use a TTF font for fixed-width text in the debugger"
[#20550]: https://github.com/hrydgard/ppsspp/issues/20550 "ImMemView: Refined the keyboard shortcuts."
[#20523]: https://github.com/hrydgard/ppsspp/issues/20523 "[ImMemView] Editable Memory"
[#20749]: https://github.com/hrydgard/ppsspp/issues/20749 "More debugger fixes"
[#21081]: https://github.com/hrydgard/ppsspp/issues/21081 "Update the rcheevos library to 1.12.2."
[#21148]: https://github.com/hrydgard/ppsspp/issues/21148 "Add workaround for infamous GoW crash"
[#21042]: https://github.com/hrydgard/ppsspp/issues/21042 "Setting out of game UI confirmation button to O makes both O and X into UI confirmation buttons"
[#21325]: https://github.com/hrydgard/ppsspp/issues/21325 "Controls: Add a setting for the threshold used to map analog stick inputs to digital buttons"
[#21306]: https://github.com/hrydgard/ppsspp/issues/21306 "Server list update, add virtual keyboard to PopupTextInputChoice"
[#21326]: https://github.com/hrydgard/ppsspp/issues/21326 "Adhoc server list: Show metadata"
[#21389]: https://github.com/hrydgard/ppsspp/issues/21389 "Adhoc server list work, add missing translations"
[#21351]: https://github.com/hrydgard/ppsspp/issues/21351 "Ad hoc server list refactor: Parse the server list from a json file"
[#21350]: https://github.com/hrydgard/ppsspp/issues/21350 "Fix multitouch in OpenGL mode on iOS. Oops."
[#21352]: https://github.com/hrydgard/ppsspp/issues/21352 "Handle adhoc relay connection a bit better"
[#21400]: https://github.com/hrydgard/ppsspp/issues/21400 "Fix minor UI centering issues and similar"
[#21362]: https://github.com/hrydgard/ppsspp/issues/21362 "More UI-related fixes"
[#21345]: https://github.com/hrydgard/ppsspp/issues/21345 "Fix image picker on Android, plus smoother fullscreen startup on Windows"
[#21384]: https://github.com/hrydgard/ppsspp/issues/21384 "Android background image selection: Scale down selected background image when too large"
[#21371]: https://github.com/hrydgard/ppsspp/issues/21371 "Various cleanups, iCloud fix"
[#21374]: https://github.com/hrydgard/ppsspp/issues/21374 "iOS: Preserve permissions to access files across runs"
[#21357]: https://github.com/hrydgard/ppsspp/issues/21357 "Add a \"hold\" version of the axis swap toggle. Often more convenient."
[#21377]: https://github.com/hrydgard/ppsspp/issues/21377 "Windows audio fix, Gripshift glitch workaround"
[#21341]: https://github.com/hrydgard/ppsspp/issues/21341 "Fix crash on audio device switch in Windows"
[#21393]: https://github.com/hrydgard/ppsspp/issues/21393 "Windows input optimizations"
[#21100]: https://github.com/hrydgard/ppsspp/issues/21100 "UWP: Migrate from C++/CX to C++/WinRT"
[#21420]: https://github.com/hrydgard/ppsspp/issues/21420 "Misc: Update aemu_postoffice, add confirmation dialog when creating game configs"
[#21409]: https://github.com/hrydgard/ppsspp/issues/21409 "Image picker on iOS: Fix crash, use a newer method that opens a lot quicker"
[#21412]: https://github.com/hrydgard/ppsspp/issues/21412 "Fix logging to file, assorted minor fixes"
[#21418]: https://github.com/hrydgard/ppsspp/issues/21418 "Networking settings: Some reordering and naming cleanup, link to quickstart guide"
[#21422]: https://github.com/hrydgard/ppsspp/issues/21422 "Fixes for some rarer crashes from Play reports"
[#21424]: https://github.com/hrydgard/ppsspp/issues/21424 "Fix control input issues when toggling the pause menu using a controller"
[#21425]: https://github.com/hrydgard/ppsspp/issues/21425 "Fix accidentally missing undo button on the savestate popup."
[#21376]: https://github.com/hrydgard/ppsspp/issues/21376 "enhance MMPX algorithm bug fixes and logic optimizations"
[#21421]: https://github.com/hrydgard/ppsspp/issues/21421 "Split MMPX texture upscaling shader into regular and advanced"
[#21426]: https://github.com/hrydgard/ppsspp/issues/21426 "Add detection of Dualsense Edge controllers on Windows, update README.md"