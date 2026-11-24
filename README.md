PPSSPP - a fast and portable PSP emulator
=========================================

Created by Henrik Rydgård

Additional code by many contributors, see the Credits screen

Originally released under the GPL 2.0 (and later) in November 2012

Official website: https://www.ppsspp.org/

Discord: https://discord.gg/5NJB6dD

No BIOS file required to play, PPSSPP is an "HLE" emulator.  Default settings balance good compatibility and speed.

To contribute, see [Introduction to development](https://www.ppsspp.org/docs/development/getting-started). This page also includes our new AI policy.

Help with testing, investigating, or fixing bugs is always welcome.  See [the list of issues](https://github.com/hrydgard/ppsspp/issues).

For the latest source code, see [our GitHub page](https://github.com/hrydgard/ppsspp).

For documentation of all kinds (usage, reference, development), see the [documentation on the main website](https://www.ppsspp.org/docs)

For build instructions and other development tutorials, see [the wiki](https://github.com/hrydgard/ppsspp/wiki).

To download fresh development builds for Android, Windows and Mac, [go to the /devbuilds page](https://www.ppsspp.org/devbuilds)

For game compatibility, see [community compatibility feedback](https://report.ppsspp.org/games).

What's new in 1.21
------------------

- Newly fully playable games
  - NFL Street 3: no longer hangs at the first autosave screen ([#22339])
  - Speedball 2 Evolution, plus the LocoRoco Midnight Carnival guide videos and other users of sceMp4 (needs an installed firmware, see below) ([#22252], [#22309])
  - Go!Edit: no more black screen after the title, and it can record video ([#22385])
  - God Eater 2 (v1.4): no longer crashes when a mission finishes loading ([#22254])
  - TOEIC Test Portable: no more white screen after the menu, and no more missing text ([#22377], [#22388])
  - Resistance: Retribution: no longer crashes after the angel boss in Bonn, so the game can be completed ([#22581])
  - Race Driver 2006: no more crashes and hangs before races in career and simulation modes ([#22327])
  - Machi: Unmei no Kousaten: no more unavoidable freeze on the third day ([#22309])
  - Tales of the World: Radiant Mythology 2: no more infinite loading when exiting to the map ([#22427])
  - Fushigi no Dungeon: Fuurai no Shiren 4 Plus (v3.10 update) now loads ([#22258])
  - Killzone: Liberation: the Chapter 5 DLC now works, installed straight from the official download ([#22565], [#22585])
  - The Chinese fan translations of Utawarerumono and Clannad now boot ([#22427], [#22432])
  - Metal Gear Solid: Peace Walker: no more black screen after the long comic-style cutscene ([#22634])
  - Fortix: no longer crashes at the title screen ([#23059])
  - Frontier Gate Boost+: no longer crashes after loading a save ([#23053])
  - Spider-Man 3: no longer crashes on some Android devices ([#23161]), and walls no longer turn red ([#23149])
  - Amagoushi no Yakata and its sequels: clicking in investigation mode works at higher rendering resolutions ([#22610], [#22883])

- PSP firmware support
  - PSP firmware can now be installed from update files shipped on game discs, or from an updater EBOOT.PBP. This happens automatically on game launch if enabled ([#22124], [#22138], [#22278])
  - With firmware installed, the XMB (PSP main menu) can be booted, on all firmware versions ([#22126], [#22281], [#22495])
  - New Tools / PSP Firmware screen showing the installed firmware ([#22271])
  - Game fonts are now taken from the firmware when available, and sceFont and a number of other libraries now run as the real firmware code, improving font rendering ([#22230], [#22282])
  - Game updates in PKG format (like the ones for God Eater 2) can now be installed by simply opening them, no more need to patch ISOs ([#22258])
  - PS1 games can be played through the firmware's own PS1 emulator (POPS), both as PSN EBOOTs and straight from disc images (cue/bin, CHD and others), running on a new emulation of the PSP's Media Engine ([#22690], [#22705], [#22707], [#22710], [#22747])
  - In the XMB: UMD Video discs play (also directly from the game list), and the Saved Data Utility, Camera, and music and video from the memory stick work ([#22714], [#22715], [#22740], [#22768], [#22903], [#23090], [#23126]). The XMB can be launched from Recents and from an Android home screen shortcut ([#22774], [#23104])
  - The ARK-5 custom firmware's XMB menus and ISO loader work, by LunaMoo ([#22787])
  - With firmware installed, the HOME screen and UI sounds can come from the firmware, by LunaMoo ([#22789], [#22807])
  - Firmware installs are much faster ([#22665], [#22974]), and you get a warning if the installed firmware is for a different PSP model ([#22913])

- Video and audio
  - Video playback has been rebuilt: sceMpeg and sceMp4 now run the game's or firmware's own code on top of a new sceVideocodec implementation. This fixes video problems in many games, like Silent Hill Origins, Mega Man Powered Up, The Warriors, Valkyrie Profile, Taiko no Tatsujin and ATV Offroad Fury Pro ([#22286], [#22309], [#22299], [#22302], [#22306], [#22318])
  - More video fixes: Need for Speed: Shift ([#21907]), Ridge Racer 1+2 ([#21793]), Idolmaster Shiny Festa chapter select ([#21924]), Ys I & II pacing ([#22359]), Buzz! Master Quiz ([#22509]), SSX On Tour ([#22515])
  - sceAudio now models the real hardware buffering, fixing crackling in FMVs in Dirt 2 and F1 2009, and popping in several other games ([#22279], [#22498])
  - MP3 streaming has been reworked, fixing Beats custom soundtracks ([#22245]), and Crazy Taxi: Fare Wars custom music now works ([#22248])
  - Fixed the music in LocoRoco 2's MuiMui house ([#22391]), reverb tuning ([#22270]), Atrac3 and audio codec fixes ([#22371], [#22348], [#23015], [#23135])
  - Games whose game list preview has its sound inside ICON1.PMF now play it, in sync with the video ([#23111])
  - "Force real clock sync" has been reworked and renamed "Low-latency CPU timing". It no longer spins a CPU core, and lowers audio and network latency ([#22838], [#23026])

- Multiplayer (ad hoc and infrastructure)
  - Ad hoc emulation has been reworked against new tests on pairs of real PSPs. Matching, discovery, game mode and timing now behave like the hardware, fixing NBA 2K13, Shaun White Snowboarding, Bomberman, Pangya: Fantasy Golf and others ([#22835], [#22843], [#22844], [#22845], [#22846], [#22847], [#22848], [#22851], [#22891], [#22894], [#22994])
  - New UDP (ENet) mode for the relay, for players with TCP connection problems, by Kethen ([#22549]). The built-in ad hoc server now includes a relay server ([#22831]), and the relay has lower latency ([#22864], [#22897]). New server: SWAENET ([#22755])
  - Infrastructure networking (sockets, DNS, access points) now behaves like the PSP's network stack ([#22823], [#22825], [#22826], [#22827], [#22828], [#22829], [#22892]). Recruiting by access point scan works in Metal Gear Solid: Portable Ops and Peace Walker ([#22961])

- CPU and kernel emulation
  - VFPU dot products and special functions (sin, cos, exp etc) are now bit-exact with the PSP, on all CPU backends. Reverse engineering work started by fp64 ([#21798], [#21815], [#22343], [#22345], [#22568])
  - Fixed the vrnds instruction, which fixes smoke and explosions in Resistance: Retribution and the Syphon Filter games ([#22571])
  - All JITs are now IR-based. The old x86-64, ARM64 and ARM32 JITs have been removed ([#22563], [#22564]), and the IR JITs and interpreter have been optimized a lot ([#22357], [#22351], [#22579], [#22587]). The JIT also compiles faster, reducing stutter on slower devices ([#23035], [#23049])
  - Loading and unloading modules is much faster, which speeds up loading in many games ([#23036])
  - A new fuzzer checks that all the CPU backends behave identically, which found and fixed many subtle bugs ([#22551], [#22554], [#22555], [#22557], [#22558])
  - JIT bookkeeping no longer overwrites game code in PSP memory. This fixes games that read or check their own code, and the savestate crash in Harry Potter and the Goblet of Fire ([#22494])
  - Thread scheduling, wait timeouts, interrupts and callbacks now match the PSP's timing much more closely, backed by new hardware tests ([#22377], [#22378], [#22383], [#22395], [#22263])
  - Savedata and other utility dialogs: lifecycle fixes, and file I/O moved off the main thread for smoother saving ([#22339], [#22349]). Correct behavior with the memory stick removed ([#22508])
  - Module loading fixes ([#22111], [#22254], [#22427], [#22507])

- Graphics
  - The vertex transform, viewport and culling pipeline has been reworked to work like the PSP. This fixes problems in many games, such as Shadow of Destiny, Frontier Gate Boost+, Sengoku Cannon, Pursuit Force, Flatout: Head On, Crisis Core, NBA 2K13, N.O.V.A., Final Fantasy Type-0 and Hokuto no Ken: Raoh Gaiden ([#21715], [#21752])
  - Specular lighting is now correct, fixing Ridge Racer and others ([#22399]), and lighting on splines (Namco Museum Battle Collection) ([#22400])
  - Improved blending accuracy ([#22465]), framebuffer fixes in Silent Hill Origins and Warriors Orochi 2 ([#22466]), CLUT framebuffer fixes (Fate/Extra shadows) ([#22482])
  - The Tantalus games (Cars Race-O-Rama, MX vs ATV, SpongeBob) no longer get dark, and render properly at higher resolutions ([#22475], [#22483])
  - Display list calls now nest like on hardware, fixing colorless hit effects in Soul Calibur ([#22552])
  - Fixed player portraits in Everybody's Golf and Hot Shots Tennis ([#22501]), lock-on in Coded Arms: Contagion ([#22520], [#22523]), custom portraits in The Sims 2 ([#22341]), map preview in Worms: Battle Islands ([#22403]), text in Umineko Portable ([#22347])
  - More fixes: GTA sprite edges ([#21788], [#21804]), Ridge Racer lens flares ([#21825]), Metal Slug and Tokiden ([#21832]), Metal Gear Solid: Portable Ops ([#21831]), texture hash collisions ([#22388])
  - Draws that need special clipping now stay on hardware transform, which fixes depth fighting and saves CPU ([#22619], [#22936])
  - The flashlight in Silent Hill: Origins and Shattered Memories now works on the hardware renderers ([#23105], [#23137], [#23139], [#23156])
  - SOCOM: Fireteam Bravo 3 no longer gets very dark in mission 4, and its sky is smooth ([#23102], [#23145]). The lens flare in Split/Second no longer shows through buildings ([#23119])
  - MSAA no longer changes how blur and glow effects look ([#22638]). New "Limit framerate to 30 fps" setting, for games that misbehave at 60 ([#23012], [#23160])
  - Workarounds for GPU driver bugs on Adreno, Mali and PowerVR ([#22912], [#22919], [#23047], [#23146], [#23150], [#23152])
  - Jackass: The Game is much faster on Vulkan ([#22547])
  - Less CPU spent on texture hashing ([#21808], [#22338]), and software depth rasterization (for lens flares in Wipeout, Syphon Filter etc) moved to a worker thread ([#22602])
  - The software renderer is now almost bit-exact with the PSP's GPU ([#22414], [#22435]), and is also much faster, close to twice as fast in heavy games on ARM ([#22433], [#22464], [#22486])
  - Hardware tessellation and hardware skinning have been removed, the CPU paths are now fast enough ([#21844], [#21921], [#22456]). The "Hardware transform" setting moved to Developer tools ([#22599])

- Other game fixes
  - MediEvil Resurrection: the dragon armour's fire breath works at 60fps ([#22617])
  - GTA: Vice City Stories: the hovercraft no longer gets stuck in "White Lies" ([#22618])
  - God of War: Ghost of Sparta: fixed endless fight in the Vortex ([#22524])
  - Crazy Taxi: Fare Wars (EU) no longer hangs ([#22327])
  - Syphon Filter: Logan's Shadow no longer crashes from explosion smoke at 60fps, and cutscenes in it and Dark Mirror play out right at 60fps ([#22717], [#22754])
  - Bejeweled 2 no longer randomly crashes after loading ([#22603]), Zuma aims properly with an analog stick ([#22731])
  - Activision Hits Remixed: the menus no longer run far too fast ([#22711]). MotoGP no longer complains about memory stick space ([#22979])
  - MX vs. ATV Untamed no longer crashes at boot without an installed firmware ([#22902]). Buzz! Quiz World question videos show ([#23071])

- User interface
  - A visual refresh with rounded corners and better layouts on large screens ([#22751], [#22760]). The settings have been reorganized into sub-pages, with much better search ([#22771], [#22779], [#22792])
  - New XMB-style list view in the game browser, now the default ([#22559], [#22968]). Icon size can be zoomed with Ctrl+wheel ([#22956]). Search also matches file names ([#22372])
  - Games can be compressed to CSO, or extracted to ISO, from the game screen, by LunaMoo ([#22616])
  - Save states are now saved and loaded on a background thread, so no more stutter ([#22667], [#22668]), and they can be given custom names ([#22290]). Loading a state can optionally ask for confirmation ([#21755])
  - Saves can be exported as zip files ([#23040])
  - Overlay (bezel) images can be drawn around the game, by LunaMoo ([#22718])
  - Gyro-stabilized tilt control, which can also drive the right analog stick ([#22706], [#22708]). The analog stick gesture can drive either stick ([#23103])
  - Download a cheat database with a single button ([#21682]), support for PSPAR (_M) cheat codes ([#22590])
  - Much better keyboard and controller navigation ([#22538]), with Tab/Shift+Tab support ([#22151])
  - The right analog stick is now mapped by default ([#21774], [#22220]). Advanced deadzone controls for analog sticks ([#21688]). Combo mappings no longer also trigger the single-button mappings they contain ([#22233])
  - Texture replacement now supports indexed (palettized) and luma-only textures, by LunaMoo ([#22574])
  - Notifications are hidden from screenshots ([#22503]), new bindable key for creating frame dumps ([#22469])
  - New languages: Khmer ([#21753]) and Norwegian Nynorsk ([#21839]), plus many translation updates, and most missing strings have been filled in ([#22809])

- Platform support
  - Windows
    - Vulkan now defaults to borderless fullscreen, which makes Auto HDR work. Exclusive fullscreen is still available ([#21616])
    - Fixes for surround-sound devices in WASAPI ([#21714], [#22023]), microphone capture through WASAPI ([#22491]), color emoji ([#21638])
    - The file browser can browse network shares ([#22749], [#22753])
  - Android
    - Audio now uses Oboe (AAudio), and follows output device changes ([#22485]). "Mix audio with other apps" is now implemented ([#21754]). TVs use OpenSL ES, fixing silent Bluetooth audio ([#22766]), and audio latency is lower ([#22889], [#23024])
    - Support for Android 17 ([#21836], [#22614]), many activity lifecycle fixes ([#22451], [#22454], [#22460], [#22487]), fixed RetroAchievements login input ([#22511])
    - TV mode: with a second display connected, the game plays there and the controls stay on the phone. Also on iOS through AirPlay ([#22728], [#22735], [#22738])
    - Custom Vulkan drivers can be used on Mali too, and new Turnip driver options ([#22810], [#22869])
    - Many crash and ANR fixes from Google Play reports ([#22654], [#22782], [#22817], [#22839])
  - Linux
    - Moved to SDL3 ([#21838]), Wayland is preferred when available ([#22238]), HTTPS support ([#22193]), microphone support ([#22459], [#22611])
    - Mixed-direction text (Arabic, Hebrew) is now rendered correctly ([#22608]). Games can be launched by dropping them on the window ([#22532])
    - The Qt frontend has been removed ([#22053])
    - The AppImage now includes PPSSPPHeadless ([#22733], [#22975]). HTTPS support on FreeBSD ([#22595])
  - iOS and Mac
    - New native Metal rendering backend ([#22659]). iOS now uses Metal only, and OpenGL and MoltenVK have been removed there ([#22034], [#22502], [#22980]). The minimum versions are now iOS 15 and macOS 12 ([#22452])
    - Microphone support on iOS ([#22490]), option to hide the status bar ([#22037]), Mac menu bar fixes ([#22346]), shortcuts use Cmd instead of Ctrl ([#22630])
  - VR
    - VR on Quest now runs on Vulkan, with both eyes drawn in a single pass. OpenGL VR has been removed ([#22934], [#23054])
  - LoongArch64 and RISC-V: now built and tested on CI, with JIT updates ([#22314], [#22586], [#21730])
  - libretro: assorted fixes, including Vulkan fast-forward crashes ([#22160], [#22544], [#22605], [#21883]). Stereoscopic and VR support, by cscd98 ([#22761], [#23045]). The Switch core is now built from this tree instead of a 2022 fork ([#22153])

- Debugger
  - The WebSocket debugger is now documented, thread-safe and has many new features ([#21984], [#22051], [#22093])
  - Support for ELF line numbers and demangled C++ symbols ([#22113], [#22144], [#22163], [#22168]), many ImDebugger improvements ([#22048], [#22050], [#22112], [#22604])

- Other
  - A large number of security and robustness fixes, many found by code review and fuzzing ([#22008], [#22012], [#22014], [#22476], [#22939], [#22940], [#22944], [#22945], [#22946], [#22947])
  - Game crashes can be reported to the report server from the crash screen ([#23044])
  - RetroAchievements fixes ([#22398], [#22244]), and Rich Presence is shown in Discord, by MackdannyTwitch ([#23157])
  - FFmpeg is now built from source as part of PPSSPP, which makes the binary about 2MB smaller ([#22785])
  - Homebrew can use extra RAM without asking for it when emulating a PSP Slim ([#22387])

What's new in 1.20.4
--------------------
1.20.4 is an unusually big followup release, but given the new multiplayer developments, we need something to cover the longer gap to 1.21.
During this cycle, I've mostly focused on UX improvements.

- Ad-hoc
  - Support for "data.json" live status from adhoc relay servers ([#21496], [#21526])
  - RetroAchievements UI improvements around subsets ([#21536])
  - Assorted ad-hoc UI improvements ([#21530], [#21510])
  - Fixes for The Warriors ([#21464]) and Street Riders ([#21460]) in relay mode

- Texture upscaling
  - Fix old bug causing some textures to get stuck unscales on some hardware when using GPU texture upscaling ([#21533])
  - Improved performance of MMPX-adv upscaler ([#21555])
  - NNEDI3 and Spline36 GPU texture scaling ([#21527])

- Rendering fixes
  - Fix a bug in lens flare occlusion for the Syphon Filter games ([#21511])
  - Fix a bug in the software renderer ([#21648])
  
- Misc UI improvements
  - Instant type-to-search in game browser ([#21559], [#21565], [#21630])
  - Fix file picker problems on some Android devices (regression) ([#21614], [#21656])
  - Fix crash in text edit fields on Mac/iOS ([#21601])
  - PSP DVD prototypes can now load directly ([#21599], [#21601])
  - Cheat UI has been cleaned up and supports titles and comments natively ([#21590])
  - RetroAchievements subset display improvements ([#21536])
  - Basic deep link support on iOS ([#21615])
  - Soft keyboard has more symbols ([#21625])

- Other
  - Fix important (but mostly rare) crash bug affecting games that draw a lot ([#21669])
  - More plugin zip files can now auto-install ([#21556])
  - Pause on lost-focus on Linux ([#21517])
  - Frameskipping no longer breaks analog stick auto-rotation in GoW, however manual rotation still broken ([#21508])
  - Android: Restore "Landscape Auto" ([#21482]), the "Auto" screen rotation mode now overrides the user rotation lock ([#21507])
  - Contribution by lrzlin: Loongarch improvements ([#21499], [#21595])
  - Basic support for extracting ISO from 7zip files ([#21493])
  - Fix a bug that prevented loading certain texture packs ([#21491])
  - Fix music looping in Death JR ([#21490])
  - Hide the save-load indicator in the corner by default ([#21528])
  - Fix crash in the remote debugger, by Nemoumbra ([#21652])
  - Fix screenshot cropping bug ([#21665])
  - mfplat.dll is no longer required on Windows unless you need camera/microphone ([#21660])

What's new in 1.20.3
--------------------

- Fix issue preventing ad hoc relay servers from working when RetroAchievements are enabled on Windows ([#21420])
- Some ad hoc UI improvements ([#21432], [#21433])
- Fix crash/failure setting the background on iOS ([#21409])
- Fix logging to file ([#21412])
- Networking settings cleanup ([#21418])
- Some fixes for assorted rare crashes ([#21422])
- Fix issues when unpausing using a controller binding ([#21424]), DualSense Edge detection on Windows ([#21426])
- Fix missing savestate undo button ([#21425])
- MMPX texture upscaling algorithm has been restored, the new one has been improved ([#21376]) and renamed MMPX Advanced ([#21421])
- Fix support for custom game config for homebrew apps ([#21434])
- Improve insets handling on Android ([#21437])

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
    - Loongarch improvements by lrzlin ([#20683], [#20644]) and KatyushaScarlet ([#20599], [#20594]), text rendering improvements ([#21163])
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
	- Add "Move to trash" deletion functionality to multiple platforms ([#20230], [#20261])
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
[#21432]: https://github.com/hrydgard/ppsspp/issues/21432 "Rename \"Hostname\" to \"Hostname or IP\" in adhoc server settings"
[#21433]: https://github.com/hrydgard/ppsspp/issues/21433 "Add a \"quick-edit\" button for the current server host/ip, if it's custom"
[#21434]: https://github.com/hrydgard/ppsspp/issues/21434 "Fix support for custom game configs for homebrews"
[#21437]: https://github.com/hrydgard/ppsspp/issues/21437 "Android: Handle inset adjustments on the C++ side, handle them better in the UI."
[#21496]: https://github.com/hrydgard/ppsspp/issues/21496 "Add live server status for servers supporting data.json"
[#21526]: https://github.com/hrydgard/ppsspp/issues/21526 "Fix json parsing bugs, add data.json for Relay Brasiliero"
[#21536]: https://github.com/hrydgard/ppsspp/issues/21536 "Show RA subsets on separate tabs"
[#21530]: https://github.com/hrydgard/ppsspp/issues/21530 "Pause screen network UI improvements"
[#21510]: https://github.com/hrydgard/ppsspp/issues/21510 "Chat popup improvements"
[#21464]: https://github.com/hrydgard/ppsspp/issues/21464 "fix the warriors in relay mode"
[#21460]: https://github.com/hrydgard/ppsspp/issues/21460 "fix street rider sending pdp packets using wrong pdp dst addresses in relay mode"
[#21533]: https://github.com/hrydgard/ppsspp/issues/21533 "Fix some textures getting stuck unscaled with GPU texture upscalers"
[#21555]: https://github.com/hrydgard/ppsspp/issues/21555 "MMPX_adv  algorithm bug fixes and logic optimizations"
[#21527]: https://github.com/hrydgard/ppsspp/issues/21527 "NNEDI3 and Spline36 GPU texture scaling"
[#21511]: https://github.com/hrydgard/ppsspp/issues/21511 "Depth raster: Handle triangles properly when backface culling is disabled"
[#21528]: https://github.com/hrydgard/ppsspp/issues/21528 "Hide the save/load indicator"
[#21556]: https://github.com/hrydgard/ppsspp/issues/21556 "Fix for auto-installing plugins from zip"
[#21517]: https://github.com/hrydgard/ppsspp/issues/21517 "Implement pause on lost focus on SDL"
[#21508]: https://github.com/hrydgard/ppsspp/issues/21508 "Analog stick autorotation now uses game timing"
[#21482]: https://github.com/hrydgard/ppsspp/issues/21482 "Restore the \"Landscape auto\" orientation mode"
[#21507]: https://github.com/hrydgard/ppsspp/issues/21507 "Android: Make the \"Auto\" rotation mode override user rotation lock (SENSOR mode)"
[#21499]: https://github.com/hrydgard/ppsspp/issues/21499 "loongarch: Implement excepetion handler and JIT bug fix"
[#21493]: https://github.com/hrydgard/ppsspp/issues/21493 "Initial 7z support"
[#21491]: https://github.com/hrydgard/ppsspp/issues/21491 "IniFile: Disallow section headers from starting at other line offsets than the start"
[#21490]: https://github.com/hrydgard/ppsspp/issues/21490 "Atrac3: Allow sending 0xFFFFFFFF as data size (Death JR music looping)"
[#21625]: https://github.com/hrydgard/ppsspp/issues/21625 "Soft keyboard work, checkboxes to enable/disable control input sources"
[#21615]: https://github.com/hrydgard/ppsspp/issues/21615 "Add deep link support on iOS"
[#21614]: https://github.com/hrydgard/ppsspp/issues/21614 "Use a proxy activity for Android file pickers, simplify handling of Vblank events"
[#21601]: https://github.com/hrydgard/ppsspp/issues/21601 "Mac/iOS: Crash and logic fixes for special characters in TextEdit"
[#21599]: https://github.com/hrydgard/ppsspp/issues/21599 "Support loading PSP prototype DVD-R disc dumps directly"
[#21590]: https://github.com/hrydgard/ppsspp/issues/21590 "UI improvements: CwCheat titles/comments, easier to scroll tabs"
[#21559]: https://github.com/hrydgard/ppsspp/issues/21559 "UI: Instant type-to-search in game browsers"
[#21565]: https://github.com/hrydgard/ppsspp/issues/21565 "Improve search to support full-width chars and to clear search on navigation"
[#21630]: https://github.com/hrydgard/ppsspp/issues/21630 "Refactor type-to-search code for reuse, use in cheats dialog"
[#21595]: https://github.com/hrydgard/ppsspp/issues/21595 "loongarch: Fix Jit_WeightsU16Skin in VertexDecoderLoongArch64.cpp"
[#21648]: https://github.com/hrydgard/ppsspp/issues/21648 "Remove 2 cases for fast path for soft gpu"
[#21656]: https://github.com/hrydgard/ppsspp/issues/21656 "Fix the java exception reporting for Android file picker errors, fix some search issues"
[#21669]: https://github.com/hrydgard/ppsspp/issues/21669 "Add \"allocation slack\" to our pushbuffers. Fixes a memory overwrite bug"
[#21652]: https://github.com/hrydgard/ppsspp/issues/21652 "Critical bug in the remote debugger"
[#21665]: https://github.com/hrydgard/ppsspp/issues/21665 "Raw screenshot cropping fix"
[#21660]: https://github.com/hrydgard/ppsspp/issues/21660 "Remove the mfplat.dll hard dependency (now fully dynamic)."
[#21616]: https://github.com/hrydgard/ppsspp/issues/21616 "Windows Vulkan: default to borderless fullscreen, add exclusive option"
[#21638]: https://github.com/hrydgard/ppsspp/issues/21638 "Windows text drawing: Use the D2D code from the UWP port so we can have color emoji"
[#21682]: https://github.com/hrydgard/ppsspp/issues/21682 "Add simple way to download cheat databases"
[#21688]: https://github.com/hrydgard/ppsspp/issues/21688 "Add Steam Input-style advanced deadzone controls to analog calibration"
[#21714]: https://github.com/hrydgard/ppsspp/issues/21714 "Wasapi improvements: Improve multi-channel mix, fix buffer allocation issue"
[#21715]: https://github.com/hrydgard/ppsspp/issues/21715 "Viewport and culling total rework"
[#21730]: https://github.com/hrydgard/ppsspp/issues/21730 "Add a CrossSIMD loongarch64 LSX implementation."
[#21752]: https://github.com/hrydgard/ppsspp/issues/21752 "Clean out NaNs and INFs from PosFloat vertex coordinates"
[#21753]: https://github.com/hrydgard/ppsspp/issues/21753 "Add Khmer language, contributed by Chhe Borin"
[#21754]: https://github.com/hrydgard/ppsspp/issues/21754 "Implement the \"Mix audio with other apps\" setting on Android."
[#21755]: https://github.com/hrydgard/ppsspp/issues/21755 "Add load state confirmation setting"
[#21774]: https://github.com/hrydgard/ppsspp/issues/21774 "Add default mappings for the right stick to many of our default keymaps"
[#21788]: https://github.com/hrydgard/ppsspp/issues/21788 "GTA sprite fix: Detect sprites, clip them slightly to avoid filtering artifacts"
[#21793]: https://github.com/hrydgard/ppsspp/issues/21793 "Fix muti ringbuffer structures for mpeg By Gemini in Android Studio"
[#21798]: https://github.com/hrydgard/ppsspp/issues/21798 "Implement (hopefully) accurate vdot instruction"
[#21804]: https://github.com/hrydgard/ppsspp/issues/21804 "Fix SpriteBorderFix problem in GTA, disable anisotropic filtering for flat draws"
[#21808]: https://github.com/hrydgard/ppsspp/issues/21808 "Texture cache: Smarter re-hashing of textures"
[#21815]: https://github.com/hrydgard/ppsspp/issues/21815 "Implement (hopefully) bitwise-exact vmul/vdiv"
[#21825]: https://github.com/hrydgard/ppsspp/issues/21825 "Fix the ugly line on Ridge Racer lens flares"
[#21831]: https://github.com/hrydgard/ppsspp/issues/21831 "Apply compat settings for TOS: FVE and Metal Gear Solid: Portable Ops"
[#21832]: https://github.com/hrydgard/ppsspp/issues/21832 "Another round of compat.ini tweaks"
[#21836]: https://github.com/hrydgard/ppsspp/issues/21836 "Implement support for Android 17 behavior changes"
[#21838]: https://github.com/hrydgard/ppsspp/issues/21838 "Port from SDL2 to SDL3"
[#21839]: https://github.com/hrydgard/ppsspp/issues/21839 "Add Norwegian Nynorsk (nn_NO) translation"
[#21844]: https://github.com/hrydgard/ppsspp/issues/21844 "Delete the \"Hardware tessellation\" feature."
[#21883]: https://github.com/hrydgard/ppsspp/issues/21883 "libretro: fix audio buffer data race and >1024-frame sample pushes"
[#21907]: https://github.com/hrydgard/ppsspp/issues/21907 "Rebase danzel's fix/mpeg-position, fix savestate issue"
[#21921]: https://github.com/hrydgard/ppsspp/issues/21921 "Remove hardware skinning (second try)"
[#21924]: https://github.com/hrydgard/ppsspp/issues/21924 "Fix MpegDemux by Gemini"
[#21984]: https://github.com/hrydgard/ppsspp/issues/21984 "Document and improve the websocket debugger (AI code by Claude)"
[#22008]: https://github.com/hrydgard/ppsspp/issues/22008 "Vulnerability fixes 1/3"
[#22012]: https://github.com/hrydgard/ppsspp/issues/22012 "Vulnerability fixes (2/3)"
[#22014]: https://github.com/hrydgard/ppsspp/issues/22014 "Vulnerability fixes (3/3)"
[#22023]: https://github.com/hrydgard/ppsspp/issues/22023 "WASAPI (Windows audio): Fix a 5.1 bug, fix a bunch more smaller bugs"
[#22034]: https://github.com/hrydgard/ppsspp/issues/22034 "Remove OpenGL support on iOS"
[#22037]: https://github.com/hrydgard/ppsspp/issues/22037 "Add setting to hide/show the status bar on iOS"
[#22048]: https://github.com/hrydgard/ppsspp/issues/22048 "ImDebugger: Lift it out of the EmuScreen"
[#22050]: https://github.com/hrydgard/ppsspp/issues/22050 "ImDebugger menu update, bugfixes"
[#22051]: https://github.com/hrydgard/ppsspp/issues/22051 "Refactor the websocket debug interface to be thread-safe, remove \"macro ops\" in disasm"
[#22053]: https://github.com/hrydgard/ppsspp/issues/22053 "Delete all support for Qt"
[#22093]: https://github.com/hrydgard/ppsspp/issues/22093 "More websocket debugger features, per-module symbol maps"
[#22111]: https://github.com/hrydgard/ppsspp/issues/22111 "Honor a module's declared segment alignment when allocating it"
[#22112]: https://github.com/hrydgard/ppsspp/issues/22112 "Debugger features: Assemble instruction popup in Im, add Run-to-here-next-frame, report breakpoint hits"
[#22113]: https://github.com/hrydgard/ppsspp/issues/22113 "Add support for ELF line numbers in debugger"
[#22124]: https://github.com/hrydgard/ppsspp/issues/22124 "PSAR file format support: Automatic unpacking of firmware updates"
[#22126]: https://github.com/hrydgard/ppsspp/issues/22126 "Boot the VSH (XMB) through HLE"
[#22138]: https://github.com/hrydgard/ppsspp/issues/22138 "Allow auto-installing firmware from EBOOT.PBP firmware updates"
[#22144]: https://github.com/hrydgard/ppsspp/issues/22144 "Debugger: Demangle C++ symbol names when loading ELF symbols"
[#22151]: https://github.com/hrydgard/ppsspp/issues/22151 "UI: Add tab navigation"
[#22160]: https://github.com/hrydgard/ppsspp/issues/22160 "Handle libretro Vulkan sync index changes at runtime"
[#22163]: https://github.com/hrydgard/ppsspp/issues/22163 "Symbol demangling: add CodeWarrior and SN Systems symbol demanglers"
[#22168]: https://github.com/hrydgard/ppsspp/issues/22168 "More symbol demangling"
[#22193]: https://github.com/hrydgard/ppsspp/issues/22193 "Enable HTTPS on Linux, through naett's libcurl backend"
[#22220]: https://github.com/hrydgard/ppsspp/issues/22220 "Assorted easy fixes for 1.21"
[#22230]: https://github.com/hrydgard/ppsspp/issues/22230 "Install sceFont fonts off the disc when we don't have them in the NAND folder"
[#22233]: https://github.com/hrydgard/ppsspp/issues/22233 "Don't fire single-button mappings while a combo using them is held"
[#22238]: https://github.com/hrydgard/ppsspp/issues/22238 "SDL: Prefer Wayland when we're in a Wayland session"
[#22244]: https://github.com/hrydgard/ppsspp/issues/22244 "Don't load RAIntegration from an install we can't write to"
[#22245]: https://github.com/hrydgard/ppsspp/issues/22245 "sceMp3: Stream buffering fixes"
[#22248]: https://github.com/hrydgard/ppsspp/issues/22248 "sceIo: generate and resolve FAT 8.3 short names"
[#22252]: https://github.com/hrydgard/ppsspp/issues/22252 "Run the real libmp4.prx/mp4msv.prx instead of our sceMp4 HLE, optionally. Fixes Speedball"
[#22254]: https://github.com/hrydgard/ppsspp/issues/22254 "Fix type-B relocations losing the shared lo16 between two HI16s"
[#22258]: https://github.com/hrydgard/ppsspp/issues/22258 "PKG install support"
[#22263]: https://github.com/hrydgard/ppsspp/issues/22263 "Hardware verified HLE fixes"
[#22270]: https://github.com/hrydgard/ppsspp/issues/22270 "Reverb effect tuning"
[#22271]: https://github.com/hrydgard/ppsspp/issues/22271 "Add a Tools/PSP Firmware screen, improve compat with older firmwares"
[#22278]: https://github.com/hrydgard/ppsspp/issues/22278 "Auto-install PSP firmware from game updaters on game launch"
[#22279]: https://github.com/hrydgard/ppsspp/issues/22279 "sceAudio: model the real buffering, so contending threads get told BUSY"
[#22281]: https://github.com/hrydgard/ppsspp/issues/22281 "Boot to XMB on all (probably) firmware versions"
[#22282]: https://github.com/hrydgard/ppsspp/issues/22282 "Run sceFont, sceDeflt, and a bunch more libraries directly without HLE"
[#22286]: https://github.com/hrydgard/ppsspp/issues/22286 "Implement sceVideocodec, run sceMpeg as LLE on top (controlled by DisableHLE)"
[#22290]: https://github.com/hrydgard/ppsspp/issues/22290 "Custom names for save states"
[#22299]: https://github.com/hrydgard/ppsspp/issues/22299 "More fixes for real sceMpeg on top of sceVideocodec (DisableHLE)"
[#22302]: https://github.com/hrydgard/ppsspp/issues/22302 "Even more fixes for real sceMpeg"
[#22306]: https://github.com/hrydgard/ppsspp/issues/22306 "Various fixes to sceMpeg LLE and sceMp3 LLE"
[#22309]: https://github.com/hrydgard/ppsspp/issues/22309 "New default: sceMpeg and sceMp4 now run as LLE (DisableHLE)"
[#22314]: https://github.com/hrydgard/ppsspp/issues/22314 "Loongarch64 and RiscV: Try to make these viable by adding cross builds"
[#22318]: https://github.com/hrydgard/ppsspp/issues/22318 "More sceMpeg fixes"
[#22327]: https://github.com/hrydgard/ppsspp/issues/22327 "Claude code review: Rework sceGe"
[#22338]: https://github.com/hrydgard/ppsspp/issues/22338 "Video texture tracking and avoiding hashing"
[#22339]: https://github.com/hrydgard/ppsspp/issues/22339 "sceUtility lifecycle fixes, GameSharing placeholder, headless fixes"
[#22341]: https://github.com/hrydgard/ppsspp/issues/22341 "Add ForceEnableGPUReadback compat By Gemini"
[#22343]: https://github.com/hrydgard/ppsspp/issues/22343 "VFPU: Replace fp64's correction tables with accurate implementation of special functions"
[#22345]: https://github.com/hrydgard/ppsspp/issues/22345 "Use the new VFPU special-function implementations globally"
[#22346]: https://github.com/hrydgard/ppsspp/issues/22346 "MacOSX: Menubar fixes, compile warning fix"
[#22347]: https://github.com/hrydgard/ppsspp/issues/22347 "Fix stale glyphs in swizzled CLUT4 atlases"
[#22348]: https://github.com/hrydgard/ppsspp/issues/22348 "Atrac: Write decoded samples when sceAtracDecodeData returns, not when called"
[#22349]: https://github.com/hrydgard/ppsspp/issues/22349 "sceUtility: Move all save/load file I/O to be off the main thread, assorted fixes"
[#22351]: https://github.com/hrydgard/ppsspp/issues/22351 "JIT function-call optimizations"
[#22357]: https://github.com/hrydgard/ppsspp/issues/22357 "IR interpreter optimizations"
[#22359]: https://github.com/hrydgard/ppsspp/issues/22359 "Video pacing investigation: Fix Ys I&II pacing by adding a cost model for video blits"
[#22371]: https://github.com/hrydgard/ppsspp/issues/22371 "sceAudiocodec and sceVideocodec timing and Atrac3+ fixes"
[#22372]: https://github.com/hrydgard/ppsspp/issues/22372 "Game browser search: also match the filename, minus extension"
[#22377]: https://github.com/hrydgard/ppsspp/issues/22377 "PSP kernel: Callback fixes"
[#22378]: https://github.com/hrydgard/ppsspp/issues/22378 "Claude hardware testing: Interrupt waits"
[#22383]: https://github.com/hrydgard/ppsspp/issues/22383 "Fixes after hardware scheduling tests by Claude"
[#22385]: https://github.com/hrydgard/ppsspp/issues/22385 "Fix Go!Edit camera hang, and some other related issues"
[#22387]: https://github.com/hrydgard/ppsspp/issues/22387 "Expose extra ram to homebrew apps if Slim model is emulated"
[#22388]: https://github.com/hrydgard/ppsspp/issues/22388 "Update xxhash to v0.8.4, use XXH3 for texture hashing"
[#22391]: https://github.com/hrydgard/ppsspp/issues/22391 "Atrac3: Correct the decoder setup for mono streams (fixes LocoRoco 2 MuiMui house music)"
[#22395]: https://github.com/hrydgard/ppsspp/issues/22395 "Claude hardware testing: Thread timing improvements"
[#22398]: https://github.com/hrydgard/ppsspp/issues/22398 "Claude code review: RetroAchievements"
[#22399]: https://github.com/hrydgard/ppsspp/issues/22399 "Correct the specular lighting math: Fix view vector, use Mitchell approximation"
[#22400]: https://github.com/hrydgard/ppsspp/issues/22400 "Spline lighting fixes"
[#22403]: https://github.com/hrydgard/ppsspp/issues/22403 "Add Worms to compat by Gemini"
[#22414]: https://github.com/hrydgard/ppsspp/issues/22414 "SoftGPU: Make it mostly bit exact vs PSP hardware"
[#22427]: https://github.com/hrydgard/ppsspp/issues/22427 "Module start/stop threads: 0x1000 default stack for kernel modules"
[#22432]: https://github.com/hrydgard/ppsspp/issues/22432 "Add some kernel NIDs and sceReg write overlay by Zcode"
[#22433]: https://github.com/hrydgard/ppsspp/issues/22433 "SoftGPU: Improve threading"
[#22435]: https://github.com/hrydgard/ppsspp/issues/22435 "SoftGPU: Implement depth swizzle (address translation), also for CPU access"
[#22451]: https://github.com/hrydgard/ppsspp/issues/22451 "Android: Replace JavaGL init with EGL"
[#22452]: https://github.com/hrydgard/ppsspp/issues/22452 "OpenGL and Vulkan rendering fixes on MacOS, upgrade MoltenVK, raise iOS/Mac min versions"
[#22454]: https://github.com/hrydgard/ppsspp/issues/22454 "More Android activity lifecycle fixes"
[#22456]: https://github.com/hrydgard/ppsspp/issues/22456 "SIMD-optimize splines and beziers"
[#22459]: https://github.com/hrydgard/ppsspp/issues/22459 "SDL: Add microphone capture and device selection"
[#22460]: https://github.com/hrydgard/ppsspp/issues/22460 "Android: Handle more activity config changes manually"
[#22464]: https://github.com/hrydgard/ppsspp/issues/22464 "SoftGPU: Render in 4x1 spans instead of 2x2 quads, like the PSP. Optimize span functions with templating"
[#22465]: https://github.com/hrydgard/ppsspp/issues/22465 "HW backends: Improve accuracy of blending"
[#22466]: https://github.com/hrydgard/ppsspp/issues/22466 "HW backends: Various framebuffer fixes"
[#22469]: https://github.com/hrydgard/ppsspp/issues/22469 "Add bindable key to create frame dumps, allow headless to save high-res screenshots"
[#22475]: https://github.com/hrydgard/ppsspp/issues/22475 "Tantalus games: Fix brightness, fix vertical stripes"
[#22476]: https://github.com/hrydgard/ppsspp/issues/22476 "Two security fixes by acts-1631, and more found by Claude"
[#22482]: https://github.com/hrydgard/ppsspp/issues/22482 "HW backends: A number of fixes to CLUT framebuffer texturing"
[#22483]: https://github.com/hrydgard/ppsspp/issues/22483 "Further improve the Tantalus games"
[#22485]: https://github.com/hrydgard/ppsspp/issues/22485 "Android: Replace OpenSL ES audio with Oboe"
[#22486]: https://github.com/hrydgard/ppsspp/issues/22486 "SoftGPU: Do an optimization pass on x86-64 too"
[#22487]: https://github.com/hrydgard/ppsspp/issues/22487 "Android lifecycle fixes, file sharing support (unused)"
[#22490]: https://github.com/hrydgard/ppsspp/issues/22490 "Add iOS microphone support"
[#22491]: https://github.com/hrydgard/ppsspp/issues/22491 "Windows: Capture the microphone through WASAPI instead of Media Foundation"
[#22494]: https://github.com/hrydgard/ppsspp/issues/22494 "JIT shadow block map: Keep JIT block entries and replacement hooks out of PSP memory"
[#22495]: https://github.com/hrydgard/ppsspp/issues/22495 "VSH: Launch unsigned homebrew from the XMB, on every firmware version"
[#22498]: https://github.com/hrydgard/ppsspp/issues/22498 "sceAudio rescheduling fix"
[#22501]: https://github.com/hrydgard/ppsspp/issues/22501 "Everybody's Golf/Tennis: Fix player portraits"
[#22502]: https://github.com/hrydgard/ppsspp/issues/22502 "iOS: Remove last remains of OpenGL support, unify sideload and App Store builds"
[#22503]: https://github.com/hrydgard/ppsspp/issues/22503 "Hide all notifications except achievements on screenshots, add Debugger menu item on Mac"
[#22507]: https://github.com/hrydgard/ppsspp/issues/22507 "sceKernelLoadModule: Put the module in memory when the wait ends (fixes SOCOM FTB2 EU)"
[#22508]: https://github.com/hrydgard/ppsspp/issues/22508 "Savedata, sceIo: Behave like hardware with the memory stick removed"
[#22509]: https://github.com/hrydgard/ppsspp/issues/22509 "Upload video frames written partway into a framebuffer. Fixes Buzz Master Quiz videos"
[#22511]: https://github.com/hrydgard/ppsspp/issues/22511 "Claude code review: Android inputbox fixes"
[#22515]: https://github.com/hrydgard/ppsspp/issues/22515 "SSX On Tour: Recognize the game's memcpy, so its videos show"
[#22520]: https://github.com/hrydgard/ppsspp/issues/22520 "Coded Arms Contagion: Add readback hock for lock-on, fix other readback issues"
[#22523]: https://github.com/hrydgard/ppsspp/issues/22523 "Add AccurateSplines compat flag, enable for Coded Arms"
[#22524]: https://github.com/hrydgard/ppsspp/issues/22524 "Fix gow vortex patch by making sure changed register value get's used"
[#22532]: https://github.com/hrydgard/ppsspp/issues/22532 "SDL: Boot a file dropped onto the window"
[#22538]: https://github.com/hrydgard/ppsspp/issues/22538 "UI: Switch to a much nicer \"beam algorithm\" for kb/controller UI navigation"
[#22544]: https://github.com/hrydgard/ppsspp/issues/22544 "Libretro core fixes"
[#22547]: https://github.com/hrydgard/ppsspp/issues/22547 "Vulkan: Batch Jackass' tiled in-place postprocess"
[#22551]: https://github.com/hrydgard/ppsspp/issues/22551 "CPU equivalence 1/6: CPUEquivalence fuzzer, and the crashes and wrong code it found"
[#22552]: https://github.com/hrydgard/ppsspp/issues/22552 "GE: Nest CALLs only two deep, like the hardware"
[#22554]: https://github.com/hrydgard/ppsspp/issues/22554 "CPU equivalence 2/6: Run every core in the guest's rounding mode and flush-to-zero"
[#22555]: https://github.com/hrydgard/ppsspp/issues/22555 "CPU equivalence 3/6: Decode shift and Allegrex bit op encodings as the PSP does"
[#22557]: https://github.com/hrydgard/ppsspp/issues/22557 "CPU equivalence 5/6: vmin/vmax ordering, vmfvc and vcmov fixes"
[#22558]: https://github.com/hrydgard/ppsspp/issues/22558 "CPU equivalence 6/6: ARM32 JIT fixes, and the docs"
[#22559]: https://github.com/hrydgard/ppsspp/issues/22559 "Game browser: Add a more XMB-like list format, make default"
[#22563]: https://github.com/hrydgard/ppsspp/issues/22563 "Remove the legacy (non-IR) x86-64 and ARM64 JITs"
[#22564]: https://github.com/hrydgard/ppsspp/issues/22564 "ARM32: Replace the legacy JIT with an IR-based one"
[#22565]: https://github.com/hrydgard/ppsspp/issues/22565 "Load game patches installed on a PSP, given its Fuse ID (Killzone: Liberation DLC)"
[#22568]: https://github.com/hrydgard/ppsspp/issues/22568 "Accurate VFPU dot products in every IR backend, on by default"
[#22571]: https://github.com/hrydgard/ppsspp/issues/22571 "VFPU: vrnds takes its seed from the vs field"
[#22574]: https://github.com/hrydgard/ppsspp/issues/22574 "Add's support for indexed textures, luma-only texture replacement"
[#22579]: https://github.com/hrydgard/ppsspp/issues/22579 "IR JIT: Multi-dot, compiling past branches, more Vec4 ops, and a sampling profiler"
[#22581]: https://github.com/hrydgard/ppsspp/issues/22581 "Resistance: Retribution: Fix the crash after the angel boss"
[#22585]: https://github.com/hrydgard/ppsspp/issues/22585 "Install Killzone: Liberation's DLC download without a PSP"
[#22586]: https://github.com/hrydgard/ppsspp/issues/22586 "RISC-V and LoongArch64 JITs: Accurate dot products, recent IR features, and fuzzer fixes"
[#22587]: https://github.com/hrydgard/ppsspp/issues/22587 "No-JIT speedups: IR interpreter block chaining, SIMD exact multi-dot, branchless skinning"
[#22590]: https://github.com/hrydgard/ppsspp/issues/22590 "Cheats: Support PSPAR (_M) codes"
[#22599]: https://github.com/hrydgard/ppsspp/issues/22599 "Move the \"hardware transform\" setting to developer tools, delete spline quality setting"
[#22602]: https://github.com/hrydgard/ppsspp/issues/22602 "GPU: Rasterize software depth on a worker thread"
[#22604]: https://github.com/hrydgard/ppsspp/issues/22604 "Debugger: Fix the Win32 VFPU window at high DPI, implement it in ImDebugger"
[#22605]: https://github.com/hrydgard/ppsspp/issues/22605 "libretro: Hand DarkStalkers' 384x224 source image to the frontend"
[#22608]: https://github.com/hrydgard/ppsspp/issues/22608 "SDL text: Reorder mixed-direction text with FriBidi"
[#22611]: https://github.com/hrydgard/ppsspp/issues/22611 "Mic: Implement sceAudioWaitInputEnd/PollInputEnd (follow-up to #22459)"
[#22614]: https://github.com/hrydgard/ppsspp/issues/22614 "Android 17: Local network permission, upload button, large-screen orientation"
[#22617]: https://github.com/hrydgard/ppsspp/issues/22617 "Fix the Medievil Dragon Armor fire at 60hz"
[#22618]: https://github.com/hrydgard/ppsspp/issues/22618 "GTA: Vice City Stories: Set the gas pedal on planes, so the hovercraft doesn't stick"
[#22153]: https://github.com/hrydgard/ppsspp/issues/22153 "Switch: build the libretro core from this tree"
[#22549]: https://github.com/hrydgard/ppsspp/issues/22549 "UDP(ENet) mode for adhocctl & aemu_postoffice relay"
[#22595]: https://github.com/hrydgard/ppsspp/issues/22595 "Add https support to FreeBSD builds (which also unbreaks builds where curl is installed and detected by find_package). Also, explicitly include <clocale> to unbreak with newer clang"
[#22603]: https://github.com/hrydgard/ppsspp/issues/22603 "Bejeweled 2: Work around race condition during loading, due to bad frame accounting"
[#22610]: https://github.com/hrydgard/ppsspp/issues/22610 "Compat flag for Amagoushi no Yakata 3D investigation hit-test readback (fixed PR)"
[#22616]: https://github.com/hrydgard/ppsspp/issues/22616 "Add built-in CSO packing functionality"
[#22619]: https://github.com/hrydgard/ppsspp/issues/22619 "GE: Make hardware and software transform depth agree more often, and keep more near/far-plane draws on the GPU"
[#22630]: https://github.com/hrydgard/ppsspp/issues/22630 "UI: Use Cmd for shortcuts on Mac and iOS, not Ctrl"
[#22634]: https://github.com/hrydgard/ppsspp/issues/22634 "sceVideocodec: Hand pictures back when the PSP's decoder does (fixes MGS Peace Walker cutscene end)"
[#22638]: https://github.com/hrydgard/ppsspp/issues/22638 "MSAA: Only multisample full size framebuffers"
[#22654]: https://github.com/hrydgard/ppsspp/issues/22654 "Fix Android crashes and ANRs from the 1.20.5-beta1 Play reports"
[#22659]: https://github.com/hrydgard/ppsspp/issues/22659 "Metal rendering backend"
[#22665]: https://github.com/hrydgard/ppsspp/issues/22665 "Firmware install: Faster through parallelism"
[#22667]: https://github.com/hrydgard/ppsspp/issues/22667 "Async savestates: compress and write on a worker thread"
[#22668]: https://github.com/hrydgard/ppsspp/issues/22668 "Savestate loads: keep JIT blocks and textures, pause menu waits for loads, review fixes"
[#22690]: https://github.com/hrydgard/ppsspp/issues/22690 "Media Engine part 1: Address space and core (off by default)"
[#22705]: https://github.com/hrydgard/ppsspp/issues/22705 "Media Engine part 6: The audio block, and what the POPS manager needs from the kernel"
[#22706]: https://github.com/hrydgard/ppsspp/issues/22706 "Tilt: Gyro-stabilized tilt on Android, right analog stick output"
[#22707]: https://github.com/hrydgard/ppsspp/issues/22707 "Boot PS1 EBOOTs through the firmware's POPS emulator"
[#22708]: https://github.com/hrydgard/ppsspp/issues/22708 "iOS: Feed the gyroscope into tilt input"
[#22710]: https://github.com/hrydgard/ppsspp/issues/22710 "POPS part 2: PS1 rendering fixes, driven by compat.ini"
[#22711]: https://github.com/hrydgard/ppsspp/issues/22711 "Activision Hits Remixed: Limit the front end to one frame per vblank"
[#22714]: https://github.com/hrydgard/ppsspp/issues/22714 "VSH: UMD discs and UMD Video in the XMB"
[#22715]: https://github.com/hrydgard/ppsspp/issues/22715 "VSH: Saved Data Utility, returning to the XMB, and network connection screen"
[#22717]: https://github.com/hrydgard/ppsspp/issues/22717 "Syphon Filter: Logan's Shadow: Fix the explosion smoke crashing at 60fps"
[#22718]: https://github.com/hrydgard/ppsspp/issues/22718 "Add overlay (bezel) support"
[#22728]: https://github.com/hrydgard/ppsspp/issues/22728 "Multi-swapchain (multi-window rendering) support, for Metal"
[#22731]: https://github.com/hrydgard/ppsspp/issues/22731 "Zuma: Correct aiming algorithm to behave better with analog sticks"
[#22733]: https://github.com/hrydgard/ppsspp/issues/22733 "AppImage: Also package PPSSPPHeadless, as an AppImage of its own"
[#22735]: https://github.com/hrydgard/ppsspp/issues/22735 "Multi-swapchain support for Vulkan (iOS TV mode, --game-window on Mac, Linux and Windows)"
[#22738]: https://github.com/hrydgard/ppsspp/issues/22738 "Android TV mode, and an 'External display' setting (iOS and Android)"
[#22740]: https://github.com/hrydgard/ppsspp/issues/22740 "Play UMD Video discs in the XMB from the game list, and fix video disc detection"
[#22747]: https://github.com/hrydgard/ppsspp/issues/22747 "POPS: Boot PS1 disc images (cue/bin, ccd/img, mds/mdf, CHD, iso) as converted EBOOTs made on the fly"
[#22749]: https://github.com/hrydgard/ppsspp/issues/22749 "PathBrowser: List the shares of a network server on Windows"
[#22751]: https://github.com/hrydgard/ppsspp/issues/22751 "UI: Introduce rounded rectangle primitive, round off the corners of settings and some main screen buttons"
[#22753]: https://github.com/hrydgard/ppsspp/issues/22753 "PathBrowser: Add a Network folder on Windows"
[#22754]: https://github.com/hrydgard/ppsspp/issues/22754 "Syphon Filter: Make cutscene timing frame rate independent (Dark Mirror, Logan's Shadow)"
[#22755]: https://github.com/hrydgard/ppsspp/issues/22755 "Another adhoc server ('SWAENET'), some logging reduction"
[#22760]: https://github.com/hrydgard/ppsspp/issues/22760 "Better layouts for large screens"
[#22761]: https://github.com/hrydgard/ppsspp/issues/22761 "Add Stereoscopic and Immersive VR to libretro core"
[#22766]: https://github.com/hrydgard/ppsspp/issues/22766 "Android: Use OpenSL ES for audio on TVs, add audio API and MMAP settings"
[#22768]: https://github.com/hrydgard/ppsspp/issues/22768 "XMB video: play MP4 and Motion JPEG from the memory stick"
[#22771]: https://github.com/hrydgard/ppsspp/issues/22771 "Settings: Build the settings screen from pages described as data"
[#22774]: https://github.com/hrydgard/ppsspp/issues/22774 "XMB: Show it in Recents, with an icon and its firmware version as the title"
[#22779]: https://github.com/hrydgard/ppsspp/issues/22779 "Setting reorg part #1: Break out Speed Hacks and PSP Settings into their own sub pages."
[#22782]: https://github.com/hrydgard/ppsspp/issues/22782 "Fixes for crashes and ANRs from the 1.21-beta1 Play reports"
[#22785]: https://github.com/hrydgard/ppsspp/issues/22785 "Build FFmpeg from a trimmed, vendored copy instead of prebuilt libraries"
[#22787]: https://github.com/hrydgard/ppsspp/issues/22787 "Ark vshctrl"
[#22789]: https://github.com/hrydgard/ppsspp/issues/22789 "Xmb home exit"
[#22792]: https://github.com/hrydgard/ppsspp/issues/22792 "Settings: General tab, pause screen save state refresh, UI polish"
[#22807]: https://github.com/hrydgard/ppsspp/issues/22807 "Firmware UI sounds"
[#22809]: https://github.com/hrydgard/ppsspp/issues/22809 "Fill in untranslated UI strings in 40 languages, fix wrong-language strings"
[#22810]: https://github.com/hrydgard/ppsspp/issues/22810 "Implement support for setting two common driver flags for Turnip (custom Adreno driver)"
[#22817]: https://github.com/hrydgard/ppsspp/issues/22817 "Fixes for crashes from the 1.20.5 beta Play reports: savedata list count, GL startup race"
[#22823]: https://github.com/hrydgard/ppsspp/issues/22823 "sceNetInet: errno per thread as on hardware, and the socket table"
[#22825]: https://github.com/hrydgard/ppsspp/issues/22825 "sceNetInet: Addresses in and out like the PSP"
[#22826]: https://github.com/hrydgard/ppsspp/issues/22826 "sceNetInet: Socket options as the PSP keeps them"
[#22827]: https://github.com/hrydgard/ppsspp/issues/22827 "sceNetInet: Poll and select as on hardware, and host sockets past FD_SETSIZE"
[#22828]: https://github.com/hrydgard/ppsspp/issues/22828 "sceNetInet: Recvmsg, Sendmsg rewritten, and sendto without an address"
[#22829]: https://github.com/hrydgard/ppsspp/issues/22829 "sceNetInet: TCP socket states and bind's port sharing"
[#22831]: https://github.com/hrydgard/ppsspp/issues/22831 "Built-in aemu_postoffice relay next to the built-in ad hoc server"
[#22835]: https://github.com/hrydgard/ppsspp/issues/22835 "Ad hoc 1/9: Socket fixes from the single-console tests"
[#22838]: https://github.com/hrydgard/ppsspp/issues/22838 "Lag sync: Fix drift and pacing. Set built-in relay to pump every millisecond"
[#22839]: https://github.com/hrydgard/ppsspp/issues/22839 "Play crash fixes: vertex range, interrupt and vtimer edge cases, GL push buffer"
[#22843]: https://github.com/hrydgard/ppsspp/issues/22843 "Ad hoc 2/9: sceNetAdhocDiscover states, GetPeerInfo on the local MAC"
[#22844]: https://github.com/hrydgard/ppsspp/issues/22844 "Ad hoc 3/9: sceNetAdhocMatching fixes from the pair tests"
[#22845]: https://github.com/hrydgard/ppsspp/issues/22845 "Ad hoc 4/9: Run the adhocctl client on the CPU thread, don't wait for the login"
[#22846]: https://github.com/hrydgard/ppsspp/issues/22846 "Ad hoc 5/9: Accurate PSP timing for adhocctl events"
[#22847]: https://github.com/hrydgard/ppsspp/issues/22847 "Ad hoc 6/9: Fail entering game mode the way a PSP does"
[#22848]: https://github.com/hrydgard/ppsspp/issues/22848 "Ad hoc 7/9: Leaving the network, and timing out a peer that left, as on a PSP"
[#22851]: https://github.com/hrydgard/ppsspp/issues/22851 "Ad hoc 9/9: Fixes from playing games, and 8x faster ad hoc tests"
[#22864]: https://github.com/hrydgard/ppsspp/issues/22864 "Ad hoc: Relay without fixed real-time waits"
[#22869]: https://github.com/hrydgard/ppsspp/issues/22869 "Android: Allow custom Vulkan drivers on Mali too"
[#22883]: https://github.com/hrydgard/ppsspp/issues/22883 "MIPSAnalyst: Add Koori no Bohyou (NPJH50186/ULJS00247) picking pass readback (Follow-up to #22514 and #22610)"
[#22889]: https://github.com/hrydgard/ppsspp/issues/22889 "Android: Minor input and audio latency improvements"
[#22891]: https://github.com/hrydgard/ppsspp/issues/22891 "Ad hoc: Fix NBA 2K13's joiner (scan list, relay self-connect, PtpFlush timing)"
[#22892]: https://github.com/hrydgard/ppsspp/issues/22892 "sceNetInet: Timed waits end late, and the other calls cost what they do on a PSP"
[#22894]: https://github.com/hrydgard/ppsspp/issues/22894 "Ad hoc: Fix Shaun White Snowboarding's game mode, send buffer occupancy, and FIXMEs settled on hardware"
[#22897]: https://github.com/hrydgard/ppsspp/issues/22897 "Ad hoc relay: Deliver back-to-back PTP sends about 1 ms apart"
[#22902]: https://github.com/hrydgard/ppsspp/issues/22902 "HLE: Fall back to our HLE when a game asks for a firmware module we haven't got"
[#22903]: https://github.com/hrydgard/ppsspp/issues/22903 "sceIo: Let pre-3.80 XMB music browsers read the memory stick"
[#22912]: https://github.com/hrydgard/ppsspp/issues/22912 "Vulkan: Work around a PowerVR crash when a vertex shader doesn't read the lights UBO"
[#22913]: https://github.com/hrydgard/ppsspp/issues/22913 "Warn when the installed firmware is for a different PSP model"
[#22919]: https://github.com/hrydgard/ppsspp/issues/22919 "Vulkan: Don't use cull distances on PowerVR"
[#22934]: https://github.com/hrydgard/ppsspp/issues/22934 "VR: OpenXR on Vulkan, with single-pass stereo (Quest)"
[#22936]: https://github.com/hrydgard/ppsspp/issues/22936 "GPU: Clip and cull on the CPU while keeping hardware transform, with CPU-lit clipped triangles"
[#22939]: https://github.com/hrydgard/ppsspp/issues/22939 "Harden file format parsers against malformed input"
[#22940]: https://github.com/hrydgard/ppsspp/issues/22940 "HLE: Range-check buffers and structs from game arguments"
[#22944]: https://github.com/hrydgard/ppsspp/issues/22944 "Ad hoc: Bound what the server and peers can make us store"
[#22945]: https://github.com/hrydgard/ppsspp/issues/22945 "Web server, HTTP client, inet: Check Origin and bound what the network can make us do"
[#22946]: https://github.com/hrydgard/ppsspp/issues/22946 "Lang files, 7z, sound effects: Bound what downloaded content can do"
[#22947]: https://github.com/hrydgard/ppsspp/issues/22947 "GE, texture packs, shaders: Bound memory access from GE commands and shared content"
[#22956]: https://github.com/hrydgard/ppsspp/issues/22956 "Game browser: Ctrl/Cmd+wheel and Ctrl/Cmd+Plus/Minus icon zoom; unify wheel units; drop SDL2 leftovers"
[#22961]: https://github.com/hrydgard/ppsspp/issues/22961 "sceNetApctl: Access point scans like a PSP's (MGS: Portable Ops and Peace Walker recruiting)"
[#22968]: https://github.com/hrydgard/ppsspp/issues/22968 "Game browser: Just grid and list, with an icon size each"
[#22974]: https://github.com/hrydgard/ppsspp/issues/22974 "KIRK: Make the ECDSA math about 700 times faster"
[#22975]: https://github.com/hrydgard/ppsspp/issues/22975 "bundle `PPSSPPHeadless` in the existing appimage"
[#22979]: https://github.com/hrydgard/ppsspp/issues/22979 "Compat: Report a small memory stick to MotoGP"
[#22980]: https://github.com/hrydgard/ppsspp/issues/22980 "iOS: Metal only (drop MoltenVK), and full debug symbols in archives"
[#22994]: https://github.com/hrydgard/ppsspp/issues/22994 "sceNetAdhocDiscover: Run the library's state machine; peers become visible when a PSP's would"
[#23012]: https://github.com/hrydgard/ppsspp/issues/23012 "Add a 'Limit framerate to 30 fps' setting, and a compat flag for it"
[#23015]: https://github.com/hrydgard/ppsspp/issues/23015 "Audio: Fix wrapping overflows, filter the SAS reverb's rate conversions"
[#23024]: https://github.com/hrydgard/ppsspp/issues/23024 "Audio: Make smooth audio use similar buffersizes to classic audio. Show stats."
[#23026]: https://github.com/hrydgard/ppsspp/issues/23026 "Rework 'Force real clock sync': Pace without spinning, shrink the audio buffer, call it Low-latency CPU timing"
[#23035]: https://github.com/hrydgard/ppsspp/issues/23035 "IR JIT: Compile ~38% faster, and measure compile stalls"
[#23036]: https://github.com/hrydgard/ppsspp/issues/23036 "Speed up module loading and unloading"
[#23040]: https://github.com/hrydgard/ppsspp/issues/23040 "Export saves as zip files, and share or save frame dumps"
[#23044]: https://github.com/hrydgard/ppsspp/issues/23044 "Report game crashes to the report server"
[#23045]: https://github.com/hrydgard/ppsspp/issues/23045 "libretro: support Vulkan stereo and immersive VR"
[#23047]: https://github.com/hrydgard/ppsspp/issues/23047 "Adreno 5xx: Work around framebuffer fetch (GL) and light ubershader (Vulkan) driver bugs"
[#23049]: https://github.com/hrydgard/ppsspp/issues/23049 "IR JITs: Compile blocks faster"
[#23053]: https://github.com/hrydgard/ppsspp/issues/23053 "JIT: Drop blocks whose code changed on a large icache invalidation"
[#23054]: https://github.com/hrydgard/ppsspp/issues/23054 "VR: Remove OpenGL VR, VR is Vulkan only"
[#23059]: https://github.com/hrydgard/ppsspp/issues/23059 "sceAac: Fix stream buffer overrun (Fortix crash), add LLE option"
[#23071]: https://github.com/hrydgard/ppsspp/issues/23071 "Fix missing question videos in Buzz! Quiz World"
[#23090]: https://github.com/hrydgard/ppsspp/issues/23090 "Camera: Make the XMB's Camera work, and fix capture restarts"
[#23102]: https://github.com/hrydgard/ppsspp/issues/23102 "SOCOM FTB3: Read back the scene luminance block when the game reads it"
[#23103]: https://github.com/hrydgard/ppsspp/issues/23103 "Gestures: Let the analog stick gesture drive either stick"
[#23104]: https://github.com/hrydgard/ppsspp/issues/23104 "Android: Let a home screen shortcut boot the XMB"
[#23105]: https://github.com/hrydgard/ppsspp/issues/23105 "Silent Hill: Origins: Fix framedump recording, and flashlight shadows and their flicker on hardware backends"
[#23111]: https://github.com/hrydgard/ppsspp/issues/23111 "Game list: Play the sound muxed into ICON1.PMF, in sync with its video"
[#23119]: https://github.com/hrydgard/ppsspp/issues/23119 "Split/Second: Read back the sun visibility buffer when the game counts it"
[#23126]: https://github.com/hrydgard/ppsspp/issues/23126 "UMD Video: Faster playback"
[#23135]: https://github.com/hrydgard/ppsspp/issues/23135 "SAS: Atrac3 voice streaming accuracy fixes"
[#23137]: https://github.com/hrydgard/ppsspp/issues/23137 "Silent Hill: Shattered Memories: Fix flashlight shadows and floor lines"
[#23139]: https://github.com/hrydgard/ppsspp/issues/23139 "Silent Hill: Origins flashlight fixes (D3D11 depth shift, TruncateBilinear)"
[#23145]: https://github.com/hrydgard/ppsspp/issues/23145 "SOCOM FTB3: Smooth the sky (CLUT8 depal ramp interpolation), enable depth raster"
[#23146]: https://github.com/hrydgard/ppsspp/issues/23146 "Vulkan: Write depth in the shader for discarding LESS draws on Adreno 6xx"
[#23149]: https://github.com/hrydgard/ppsspp/issues/23149 "Screen-linear colors for Spider-Man 3 (compat flag), and D3D11 depth rounding fix"
[#23150]: https://github.com/hrydgard/ppsspp/issues/23150 "OpenGL: Blit depth instead of drawing it on Adreno"
[#23152]: https://github.com/hrydgard/ppsspp/issues/23152 "Vulkan: On Mali, keep the depth when only the stencil is cleared at bind"
[#23156]: https://github.com/hrydgard/ppsspp/issues/23156 "Fix EQUAL depth redraws: Origins vertex snapping, Mali GL depth blit, PowerVR shader EQUAL"
[#23157]: https://github.com/hrydgard/ppsspp/issues/23157 "Log RetroAchievements Rich Presence and display it in Discord"
[#23160]: https://github.com/hrydgard/ppsspp/issues/23160 "Fix uneven 30fps when a game flips between halves of one buffer"
[#23161]: https://github.com/hrydgard/ppsspp/issues/23161 "Fix a Spider-Man 3 crash on Android, and make runs agree across hosts and sessions"
