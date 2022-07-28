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

For the latest source code, see [our GitHub page](https://github.com/hrydgard/ppsspp).

For build instructions and other development tutorials, see [the wiki](https://github.com/hrydgard/ppsspp/wiki).

If you want to download regularly updated builds for Android, Windows x86 and x64, proceed to this [page](https://buildbot.orphis.net/ppsspp/)

For game compatibility, see [community compatibility feedback](https://report.ppsspp.org/games).

What's new in 1.13.1
====================

* Confirmation dialog added before change of MAC address ([#15738])
* IR interpreter regression fixed ([#15739])
* Fix clearing of replacement texture cache ([#15740])
* Improved Portuguese-pt translation ([#15734])
* Fix graphical regression in Split/Second ([#15733])
* Couple of minor crash fixes

What's new in 1.13
==================

General

* Fix assorted Android "scoped storage"-related bugs and performance issues ([#15237], [#15487]), etc.
* Analog mapping for fast-forward ([#15645])
* Major softgpu accuracy fixes and speedups, including a JIT ([#15163], [#15345], [#15389], [#15529], [#15440], [#15410], [#15405], [#15400]) and many, many more
* Fixed some NEON code paths ([#15481])
* Fix performance of texture uploads with Vulkan ([#15474])
* Don't include the large font atlas when we don't need it
* Improved upscaling shaders ([#15566])
* Vulkan texture upscaling performance improvements ([#15238]), etc.
* Vulkan correctness fixes ([#15217], [#15211]), use the VMA allocator ([#15162]), etc.
* Fixes to depth culling ([#15106]), many more
* Background loading of texture replacement ([#15025])
* Threading manager improvements and fixes ([#15470]), etc.
* Added search in settings ([#14414])
* Added fast button repeats on custom touch buttons ([#15613])
* Two new bicubic upscaling shader: Catmull-Rom and Mitchell-Netravali ([#15569])
* Allow to change screen rotation per game and to bind a key to change it ([#15494], [#15510])
* Re-enabled software rendering option on Android ([#12958])

Game fixes

* Add more workarounds for Mali driver bugs ([#15016])
* Vortex in God of War: Ghost of Sparta can now be passed ([#15640])
* Various proAdhoc fixes ([#15213], [#15215]), and many more
* Correct flickering text in Sol Trigger and Last Ranker. ([#15549])
* Fix and improve line drawing in Echochrome ([#15583]), after line refactoring ([#15073], [#15075])
* Fix HUD graphics in Split/Second ([#15500], [#15501])
* Fix bad screen overlay issues in Clone Wars and Force Unleashed ([#15691], [#15696], [#12949], [#9572])
* Zettai Zetsumei Toshi 3 no longer hangs on character select screen ([#15687])
* Juiced 2: Bloom effect no longer covering the screen ([#7295], [#15717])
* Fix keyboard shift issue in a few games ([#15698])

UI

* Windows/Xbox UWP directory navigation improvements ([#15652])
* Color change and basic theme support ([#15396], [#15394])
* Fix input focus bug ([#15560])
* New GE debugger features and other UI fixes ([#15393], [#15324], [#15377], [#15424], [#15402], [#15378], [#15338]), etc.

What's new in 1.12.3
====================
* Fix background music speed. A couple translation fixes.

What's new in 1.12.2
====================
* Fix joystick detection bug on Android.

What's new in 1.12.1
====================
* Bug fixes (control mapping fix, popup menus in the Windows debugger, a few crashfixes)

What's new in 1.12
==================

Platform support:
* Add support for Android 12 Scoped Storage restrictions ([#11997])
* iOS: Fix multitouch tracking ([#5099])
* Android: Fix screenshot orientation on Vulkan ([#14053])
* Linux: Improve support for system FFmpeg 3.1+ ([#14176], [#14188], [#14199])
* libretro: Always enable function hooks ([#14145])
* AMD: Enable Vulkan rendering on a thread ([#13864])
* Add iOS version detection, turn off JIT on bootup if >= 14.3. ([#14201])
* iOS: Try a different JIT detection method, thanks Halo-Michael ([#14241])
* Windows: Restore window size correctly ([#14317])

Game fixes:
* Fix NBA Live 08 loading ([#8288])
* Display Open Season title screen correctly ([#13252])
* Fix Metal Gear Solid Peace Walker Chinese Patched blue screen ([#14127])
* Load Ape Academy 2 correctly ([#14271])
* Many more...

Graphics and Sound:
* Add new texture filtering mode "Auto Max Quality" ([#14789])
* Fix Princess Maker 5 Portable half screen in Vulkan ([#13741])
* Fix Pro Yakyu Spirits 2010 (NPJH50234): Rendering errors with hardware transform off ([#14167])
* Support texture replacement filtering overrides ([#14230])
* Fix Yarudora Portable: Double Cast's FMVs artifacting ([#13759])
* Fix Sims 2 Castaway/Pets EA Logo glitched out ([#13146])
* Fix bad size & position on Japanese & Numbers & Alphabets ([#14209])
* Implement basic depth texturing for OpenGL ([#14042])
* Google Cardboard fixes ([#14966], [#14768])
* Correct mini-map update in Z.H.P. ([#14069])
* Fix crash in vertex jit on ARM32 ([#14879])
* Add a setting for reverb volume ([#14711])
* Option to switch to new devices or not, on Windows.

UI:
* Add a setting for choosing background animation in PPSSPP's menus ([#14313], [#14818], [#14810], [#14347])
* Add CRC calculation on game info screen and feedback screen ([#14000], [#14041])
* Add a Storage tab to System Information with some path info ([#14224], [#14238])
* Track and show memory allocation / usage information in debugger ([#14056])
* Allow searching within the savedata manager ([#14237])
* Enable postshaders to access previous frame ([#14528])
* Add missing Japanese keyboard symbol ([#14548])
* Add Reset button on crash screen, allow load state and related ([#14708])
* Implement save state load and save undo ([#14676], [#14679], [#14697])
* A lot of minor debugger improvements

Controls:
* New analog stick calibration menu ([#14596])
* Improved combo button and moved settings to Customize Touch Control -> Customize -> Custom button ([#13869])
* Improved tilt control, allow to change axis ([#12530])
* Add a visual means of control mapping ([#14769])
* Add basic motion gesture support ([#13107])
* Fix touch control DPAD not getting input when dragged over, and make touch analog drag not activate other buttons ([#14843])
* Allow adjusting touch control analog stick head size ([#14480])

Adhoc/Network:
* Fix multiplayer issue on MGS:PW due to detecting an incorrect source port on incoming data ([#14140])
* Always enable TCPNoDelay to improve response time ([#14235])
* Fix Teenage Mutant Ninja Turtles multiplayer ([#14284])
* Fix FlatOut Head On multiplayer ([#14290])
* Prevent flooding Adhoc Server with connection attempts ([#14335])
* Fix crashing issue when leaving a multiplayer game room (ie. GTA Vice City Stories) ([#14342])
* Fix stuck issue when scanning AP to Recruit on MGS:PW ([#14345])
* Fix possible crash issue on blocking socket implementation (ie. Kao Challengers) ([#14466])
* Create GameMode's socket after Master and all Replicas have been created (ie. Fading Shadows) ([#14492])
* Reduce HLE delays due to multiplayer performance regressions (ie. Ys vs. Sora no Kiseki) ([#14513])
* Fix socket error 10014 on Windows when hosting a game of Vulcanus Seek and Destroy ([#14849])

What's new in 1.11.3
====================
* Fix for graphics glitches in the on-screen keyboard

What's new in 1.11.2
====================
* An additional few crash fixes ([#14129], [#14134], [#14132])

What's new in 1.11.1
====================
* A few crash fixes ([#14085], [#14089], [#14091], [#14092]), a few adhoc fixes
* Glitchy menu audio on some devices ([#14101]), in-game UI font memory leak ([#14078])
* Couple of adhoc fixes ([#14106], [#14117])

What's new in 1.11.0
====================
* Lots of minor bug fixes, crash fixes, and performance fixes and improvements.
* New Browse... button to allow opening SD cards on Android 11
* Countless AdHoc networking fixes by ANR2ME, for example Dragon Ball Shin Budokai, PowerStone,
  Bleach Heat The Soul 7, Kingdom Hearts, GTA: VCS and many more.
* Graphics issue with car reflections fixed in Outrun, Dirt 2 ([#13636], [#13640], [#13760])
* Cut-off cards in Yu Gi Oh fixed ([#7124]).
* Numerous fixes to the builtin fonts by nassau-tk
* Added exception handler so PPSSPP stays alive if a game crashes ([#11795]/[#13092])
* Desktop: Support for multiple instance multiplayer ([#13172], ...)
* Workaround for rendering bugs with flat shading in iOS 14
* Multiple fixes to the IR interpreter ([#13897], ...)
* UI: New fullscreen button on desktop platforms, optional navigation sounds ([#13239])
* Audio and multiple hangs fixes in UWP version ([#13792], ...)
* Partial microphone support ([#12336], ...)
* Workaround for wacky action mirroring bug in Hitman Reborn Battle Arena 2 ([#13706], [#13526])
* Hardware texture upscaling for Vulkan, mipmap generation ([#13235], [#13514])
* Added MMPX Vulkan texture upscaling shader ([#13986])
* Depth texturing support in Vulkan and D3D11 ([#13262], [#13556], ...)
* Performance fix for Test Drive Unlimited ([#13355], ...)
* Allow rewind on mobile ([#13866])
* Added option to disable on-screen messages ([#13695])
* Added "Lower resolution for effects" on libretro ([#13654])
* Allow chaining multiple post-processing shaders ([#12924])
* Support for loading game-specific plugins ([#13335])
* Fixed Assassin's Creed: Bloodlines Save issue on Android ([#12761])
* Hanayaka Nari Wa ga Ichizoku: mono voices fixed ([#5213])
* Additional fixed games:
    * Namco Museum - Battle Collection, Vol 2 ([#9523], [#13297], [#13298])
    * Dream Club Portable (graphics bugs, GL and Vulkan) ([#6025])
    * Capcom Classic Collection Reloaded (stuck in return game) ([#4671])
    * Xyanide Resurrection (freezing) ([#8526])
    * Dissidia Final Fantasy Chinese (patched game, invalid address) ([#13204])
    * Crazy Taxi ([#13368])
    * Spiderman: Friend or Foe ([#13969])
    * Downstream Panic (US) (New Game crash) ([#13633])

What's new in 1.10.3
--------------------
* Fix for control layout editor ([#13125])

What's new in 1.10.2
--------------------
* More crashfixes ([#13094], [#13093])
* Improve download performance and cancel behavior ([#13095])
* Restore the removed I/O on Thread option ([#13096])

What's new in 1.10.1
--------------------
* Fixes for common crashes ([#13077], [#13076], see [#13057])
* Fix for offset rendering in D3D9 ([#13071])

What's new in 1.10.0
--------------------
* Graphics and compatibility fixes ([#12800], [#12670], [#12635], [#12857], [#12941], [#11898], [#12695], more)
* Assorted minor performance improvements, game load speedup ([#12462], [#12652])
* Screen inset (notch) support on Android ([#12779])
* Analog stick support for menu navigation ([#12685])
* Fixed audio glitches in SDL builds ([#12916], [#12920])
* Support more languages in in-game dialogs ([#12702]). Croatian language added to PPSSPP.
* Simple multiplayer chat ([#12667])
* More advanced postprocessing (multipass, parameters) ([#12905], [#12901])
* Add PPSSPP-specific CWCheat ([#12816], [#12912])
* Reintroduce Cardboard VR, allow more resolutions ([#12449], [#8714])
* Fix some crashes ([#12908], [#12876])
* Ghost in the Shell graphics fixed (JIT inaccuracy with inf * 0) ([#12519])
* Mac build now supports Vulkan on top of MoltenVK ([#12583])
* Raspberry Pi 4 EGL crash fixed ([#12474])
* VSync now supported on all backends, frame duplication option added for 30 Hz games ([#12659], [#12602])
* Camera supported on Windows, Linux and Mac (still no microphone though) ([#12572], [#12580], [#12607])
* Darkstalkers fixed and working through software rendering. SW rendering fixed on GLES 2.0 ([#12443], [#12898])
* Hot Shots Golf slowdown and flicker on Vulkan fixed ([#12873], [#12746])
* Pangya Golf crashes and hangs fixed ([#12718])
* Allow rebinding of right touch screen analog ([#12486])
* Add option to prevent mipmaps from being dumped ([#12818])
* Tilt control now have a base radius to help with deadzone ([#12756])
* Mappable auto rotating analog stick to pass some game checks ([#12749])
* Touch control position can now be snapped to a grid ([#12517])
* HiDPI retina display support ([#12552])
* Rapid-fire on touch control ([#12601])
* Toggle mute button ([#12643])
* Add option to resize game icons and more ([#12646], [#12637])
* Frames in-flight now configurable to reduce input lag at the cost of speed ([#12660])
* Add toggle mode to combo button ([#12623])
* SDL mouse support, Qt menu upgrades ([#12612], [#12817])
* Real support for Chinese patched version of Hatsune Miku Project Diva Extend ([#13007])
* Some minor kernel module support ([#13028], [#12225], [#13026], [#13004], [#13038], [#13023])
* Fixed fullscreen toggling with Vulkan in SDL builds ([#11974])


Looking for [older news](history.md)?


Adhoc support
-------------
Not fully functional, but some games work.  Check the [Ad-Hoc section of the forum](http://forums.ppsspp.org/forumdisplay.php?fid=34) for help.

Credit goes to:
 - ANR2ME
 - Igor Calabria
 - [coldbird's code](https://code.google.com/archive/p/aemu/)
 - Kyhel
 - And more, of course.


[comment]: # (LINK_LIST_BEGIN_HERE)
[#13125]: https://github.com/hrydgard/ppsspp/issues/13125 "Refactor and fix touch control layout screen for notch"
[#13094]: https://github.com/hrydgard/ppsspp/issues/13094 "Camera initialization crash fix"
[#13093]: https://github.com/hrydgard/ppsspp/issues/13093 "Add a try/catch to Android camera device listing."
[#13095]: https://github.com/hrydgard/ppsspp/issues/13095 "http: Check cancel flag more often"
[#13096]: https://github.com/hrydgard/ppsspp/issues/13096 "Revert \"Remove the I/O on Thread option - treat it as always on.\""
[#13077]: https://github.com/hrydgard/ppsspp/issues/13077 "SaveState: Make sure to default init net data"
[#13076]: https://github.com/hrydgard/ppsspp/issues/13076 "Add some excessive null checks to GameScreen::render()"
[#13057]: https://github.com/hrydgard/ppsspp/issues/13057 "The 1.10 Android mystery crash thread!"
[#13071]: https://github.com/hrydgard/ppsspp/issues/13071 "D3D9: Fix a sign mistake generating the projection matrix."
[#12800]: https://github.com/hrydgard/ppsspp/issues/12800 "x86jit: Force INF * 0 to +NAN"
[#12670]: https://github.com/hrydgard/ppsspp/issues/12670 "Attempts to replace 0 frame width with valid frame width.(sceMpegAvcCsc)"
[#12635]: https://github.com/hrydgard/ppsspp/issues/12635 "Kernel: Delay better in sceKernelReferThreadStatus"
[#12857]: https://github.com/hrydgard/ppsspp/issues/12857 "Mumbo Jumbo games freeze on loading screen since v1.6"
[#12941]: https://github.com/hrydgard/ppsspp/issues/12941 "Vulkan: Deal with the reformat clear better"
[#11898]: https://github.com/hrydgard/ppsspp/issues/11898 "Strike Witches - Hakugin no Tsubasa  missing intro video"
[#12695]: https://github.com/hrydgard/ppsspp/issues/12695 "New heuristic for getting rid of unnecessary \"antialias-lines\"."
[#12462]: https://github.com/hrydgard/ppsspp/issues/12462 "Vulkan: Enable renderpass merging for all games"
[#12652]: https://github.com/hrydgard/ppsspp/issues/12652 "ScanForFunctions: Speed up game loading"
[#12779]: https://github.com/hrydgard/ppsspp/issues/12779 "Support drawing around notches on Android displays. Fixes #12261"
[#12685]: https://github.com/hrydgard/ppsspp/issues/12685 "UI: Simple joystick navigation. Fixes #10996."
[#12916]: https://github.com/hrydgard/ppsspp/issues/12916 "More audio buffering fixes (primarily affects SDL)"
[#12920]: https://github.com/hrydgard/ppsspp/issues/12920 "Remove the Audio Resampling setting (now always on)."
[#12702]: https://github.com/hrydgard/ppsspp/issues/12702 "PPGe: Use TextDrawer for save UI if available"
[#12667]: https://github.com/hrydgard/ppsspp/issues/12667 "Chat feature based on Adenovan's Rechat branch"
[#12905]: https://github.com/hrydgard/ppsspp/issues/12905 "Allow chained post-processing shaders"
[#12901]: https://github.com/hrydgard/ppsspp/issues/12901 "Post shader setting uniform"
[#12816]: https://github.com/hrydgard/ppsspp/issues/12816 "Implement Xinput vibration CWCheat (PPSSPP specific 0xA code type)"
[#12912]: https://github.com/hrydgard/ppsspp/issues/12912 "Add CWCHEAT for postprocessing"
[#12449]: https://github.com/hrydgard/ppsspp/issues/12449 "Reintroduce Cardboard VR"
[#8714]: https://github.com/hrydgard/ppsspp/issues/8714 "Allow > 5x PSP resolution for devices like iPad Pro 12.9"
[#12908]: https://github.com/hrydgard/ppsspp/issues/12908 "Fix \"Improved compatibility of sceGeListEnQueue: verify that stackDepth < 256\""
[#12876]: https://github.com/hrydgard/ppsspp/issues/12876 "Windows: Add safety checks to WASAPI code"
[#12519]: https://github.com/hrydgard/ppsspp/issues/12519 "Ghost In The Shell - Stand Alone Complex (ULUS10020) - Black Textures and missing screens."
[#12583]: https://github.com/hrydgard/ppsspp/issues/12583 "macOS: Initial support for vulkan on macOS ( MoltenVK )"
[#12474]: https://github.com/hrydgard/ppsspp/issues/12474 "Egl bug on rpi4 with master mesa?"
[#12659]: https://github.com/hrydgard/ppsspp/issues/12659 "Support vsync in all hardware backends, support runtime update"
[#12602]: https://github.com/hrydgard/ppsspp/issues/12602 "Add option to improve frame pacing through duplicate frames if below 60hz."
[#12572]: https://github.com/hrydgard/ppsspp/issues/12572 "Add camera support for windows."
[#12580]: https://github.com/hrydgard/ppsspp/issues/12580 "Add camera support for linux (V4L2)"
[#12607]: https://github.com/hrydgard/ppsspp/issues/12607 "QT API for camera (Linux/macOS)"
[#12443]: https://github.com/hrydgard/ppsspp/issues/12443 "Darkstalkers Chronicle: Add specializations and speedhacks to get it kinda playable"
[#12898]: https://github.com/hrydgard/ppsspp/issues/12898 "[Android] [Mali GPU] [OpenGL] Lastest build blackscreen on buffered rendering mode"
[#12873]: https://github.com/hrydgard/ppsspp/issues/12873 "Vulkan: Framebuffer manager: Use an allocator for \"MakePixelTexture\" images."
[#12746]: https://github.com/hrydgard/ppsspp/issues/12746 "GPU: Assume a scissor of 481x273 is a mistake"
[#12718]: https://github.com/hrydgard/ppsspp/issues/12718 "Vpl: Correct allocation order when splitting block"
[#12486]: https://github.com/hrydgard/ppsspp/issues/12486 "Rebindable touch right analog"
[#12818]: https://github.com/hrydgard/ppsspp/issues/12818 "Add option to prevent Mipmaps from being dumped"
[#12756]: https://github.com/hrydgard/ppsspp/issues/12756 "Skip deadzone option on tilt"
[#12749]: https://github.com/hrydgard/ppsspp/issues/12749 "Auto rotating analog"
[#12517]: https://github.com/hrydgard/ppsspp/issues/12517 "Touch control grid snap"
[#12552]: https://github.com/hrydgard/ppsspp/issues/12552 "Qt/macOS: enable HiDPI ( retina display ) support"
[#12601]: https://github.com/hrydgard/ppsspp/issues/12601 "Add rapid fire to touch control"
[#12643]: https://github.com/hrydgard/ppsspp/issues/12643 "Toggle mute button"
[#12646]: https://github.com/hrydgard/ppsspp/issues/12646 "Resizable game icons"
[#12637]: https://github.com/hrydgard/ppsspp/issues/12637 "Region flag and game ID on game selection screen"
[#12660]: https://github.com/hrydgard/ppsspp/issues/12660 "GPU: Add setting to control inflight frame usage"
[#12623]: https://github.com/hrydgard/ppsspp/issues/12623 "Add toggle flag to combo button"
[#12612]: https://github.com/hrydgard/ppsspp/issues/12612 "SDL analog mouse input"
[#12817]: https://github.com/hrydgard/ppsspp/issues/12817 "Unification of the menu of Linux and Windows versions"
[#13007]: https://github.com/hrydgard/ppsspp/issues/13007 "Real support \"Hatsune Miku Project Diva Extend\" chinese patched version"
[#13028]: https://github.com/hrydgard/ppsspp/issues/13028 "Real support Code Geass: Lost Colors chinese patched version"
[#12225]: https://github.com/hrydgard/ppsspp/issues/12225 "Rebased: Wrap some SysMemForKernel's nids, fixing #7960"
[#13026]: https://github.com/hrydgard/ppsspp/issues/13026 "Add some ThreadManForKernel nids"
[#13004]: https://github.com/hrydgard/ppsspp/issues/13004 "Warp some ThreadManForKernel and sceKernelExitVSHKernel"
[#13038]: https://github.com/hrydgard/ppsspp/issues/13038 "Add sysclib_strncmp,sysclib_memmove"
[#13023]: https://github.com/hrydgard/ppsspp/issues/13023 "Add sysclib_strstr"
[#11974]: https://github.com/hrydgard/ppsspp/issues/11974 "[Linux] [Vulkan] Toggle fullscreen doesn't update display properly"
[#13636]: https://github.com/hrydgard/ppsspp/issues/13636 "Reinterpret framebuffer formats as needed. Outrun reflections partial fix"
[#13640]: https://github.com/hrydgard/ppsspp/issues/13640 "Fix car reflections in Outrun"
[#13760]: https://github.com/hrydgard/ppsspp/issues/13760 "Fix car lighting issues in DiRT 2."
[#7124]: https://github.com/hrydgard/ppsspp/issues/7124 "Yu-Gi-Oh! GX Tag Force Card summoning (card cut-off / cropped)"
[#11795]: https://github.com/hrydgard/ppsspp/issues/11795 "Exception handler - catch bad memory accesses"
[#13092]: https://github.com/hrydgard/ppsspp/issues/13092 "Bad memory access handling improvements"
[#13172]: https://github.com/hrydgard/ppsspp/issues/13172 "Generalized multi-instance"
[#13897]: https://github.com/hrydgard/ppsspp/issues/13897 "LittleBigPlanet - Game Not Loading, Blue Screen (iOS, Unplayable)"
[#13239]: https://github.com/hrydgard/ppsspp/issues/13239 "Add sound effects for PPSSPP interface navigation"
[#13792]: https://github.com/hrydgard/ppsspp/issues/13792 "Fix UWP audio and a hang bug"
[#12336]: https://github.com/hrydgard/ppsspp/issues/12336 "Microphone support"
[#13706]: https://github.com/hrydgard/ppsspp/issues/13706 "Add back the old implementation of vfpu_sin/cos/sincos."
[#13526]: https://github.com/hrydgard/ppsspp/issues/13526 "VFPU: Compute sines and cosines in double precision."
[#13235]: https://github.com/hrydgard/ppsspp/issues/13235 "Vulkan: Allow custom texture upscaling shaders"
[#13514]: https://github.com/hrydgard/ppsspp/issues/13514 "Vulkan: Automatically generate mipmaps for replaced/scaled textures"
[#13986]: https://github.com/hrydgard/ppsspp/issues/13986 "Vulkan: Add MMPX upscaling texture shader"
[#13355]: https://github.com/hrydgard/ppsspp/issues/13355 "Refactor framebuffer attachment. Fixes Test Drive Unlimited performance"
[#13866]: https://github.com/hrydgard/ppsspp/issues/13866 "SaveState: Allow rewind on mobile"
[#13695]: https://github.com/hrydgard/ppsspp/issues/13695 "Add developer setting \"Show on-screen messages\". Uncheck to hide them."
[#13654]: https://github.com/hrydgard/ppsspp/issues/13654 "Expose the \"Lower resolution for effects\" setting in libretro."
[#12924]: https://github.com/hrydgard/ppsspp/issues/12924 "Postprocessing: User chain support"
[#13335]: https://github.com/hrydgard/ppsspp/issues/13335 "Support for loading game-specific plugins"
[#12761]: https://github.com/hrydgard/ppsspp/issues/12761 "[Android][OpenGL&Vulkan][Save issue] Assassin's Creed : Bloodlines (ULJM05571)"
[#5213]: https://github.com/hrydgard/ppsspp/issues/5213 "Hanayaka Nari Wa ga Ichizoku strange MP3 mono voice"
[#9523]: https://github.com/hrydgard/ppsspp/issues/9523 "Namco Museum - Battle Collection - ULUS100035 loading problem"
[#13297]: https://github.com/hrydgard/ppsspp/issues/13297 "Namco Museum Vol. 2 - ULJS00047 infinite loading in some game"
[#13298]: https://github.com/hrydgard/ppsspp/issues/13298 "Fix sceKernelExitThread"
[#6025]: https://github.com/hrydgard/ppsspp/issues/6025 "Dream Club Portable crash after select girl"
[#4671]: https://github.com/hrydgard/ppsspp/issues/4671 "Capcom Classic Collection Reloaded stuck in return game"
[#8526]: https://github.com/hrydgard/ppsspp/issues/8526 "Xyanide Resurrection freezing"
[#13204]: https://github.com/hrydgard/ppsspp/issues/13204 "Dissidia Final Fantasy Chinese patch invalid address"
[#13368]: https://github.com/hrydgard/ppsspp/issues/13368 "Reschedule after resuming thread from suspend."
[#13969]: https://github.com/hrydgard/ppsspp/issues/13969 "Io: Don't allow async close while async busy"
[#13633]: https://github.com/hrydgard/ppsspp/issues/13633 "Downstream Panic (US) New Game crashes"
[#13262]: https://github.com/hrydgard/ppsspp/issues/13262 "Implement texturing from depth buffers (Vulkan only so far)"
[#13556]: https://github.com/hrydgard/ppsspp/issues/13556 "D3D11 depth texture support"
[#14085]: https://github.com/hrydgard/ppsspp/issues/14085 "Handle exec addr errors better - don't let IgnoreBadMemoryAccesses skip dispatcher exceptions"
[#14089]: https://github.com/hrydgard/ppsspp/issues/14089 "GL: Call CreateDeviceObjects *after* updating render_."
[#14091]: https://github.com/hrydgard/ppsspp/issues/14091 "Only allow sceMpegGetAvcAu warmup for God Eater Series"
[#14092]: https://github.com/hrydgard/ppsspp/issues/14092 "SaveState: Prevent crash on bad cookie marker"
[#14101]: https://github.com/hrydgard/ppsspp/issues/14101 "Menu audio glitchfix"
[#14078]: https://github.com/hrydgard/ppsspp/issues/14078 "PPGe: Decimate text images properly"
[#14106]: https://github.com/hrydgard/ppsspp/issues/14106 "[Adhoc] Fix frozen (0 FPS) issue on Kao Challengers and Asterix & Obelix XX"
[#14117]: https://github.com/hrydgard/ppsspp/issues/14117 "[Adhoc] Fix lob"
[#14129]: https://github.com/hrydgard/ppsspp/issues/14129 "GPU: Force reinterpret off without copy image"
[#14134]: https://github.com/hrydgard/ppsspp/issues/14134 "Android: Ensure shutdown waits for render"
[#14132]: https://github.com/hrydgard/ppsspp/issues/14132 "Io: Truncate reads/writes to valid memory"
[#11997]: https://github.com/hrydgard/ppsspp/issues/11997 "Android 12 scoped storage hell"
[#5099]: https://github.com/hrydgard/ppsspp/issues/5099 "IOS touch controls problems."
[#14053]: https://github.com/hrydgard/ppsspp/issues/14053 "Toggle screenshot minor issue."
[#14176]: https://github.com/hrydgard/ppsspp/issues/14176 "Remove deprecated API calls for new FFmpeg 4.3.x"
[#14188]: https://github.com/hrydgard/ppsspp/issues/14188 "Additional fixes for FFmpeg 3.1+"
[#14199]: https://github.com/hrydgard/ppsspp/issues/14199 "Mpeg: Set low latency flag for video decode"
[#14145]: https://github.com/hrydgard/ppsspp/issues/14145 "libretro: Remove \"Unsafe FuncReplacements\" option."
[#13864]: https://github.com/hrydgard/ppsspp/issues/13864 "Vulkan: Remove #10097 hack for newer AMD drivers"
[#14201]: https://github.com/hrydgard/ppsspp/issues/14201 "Add iOS version detection, turn off JIT on bootup if >= 14.3."
[#14241]: https://github.com/hrydgard/ppsspp/issues/14241 "iOS: Try a different JIT detection method, thanks Halo-Michael."
[#14317]: https://github.com/hrydgard/ppsspp/issues/14317 "Window size restarts on closing"
[#8288]: https://github.com/hrydgard/ppsspp/issues/8288 "NBA live 08 Invalid address and hang"
[#14069]: https://github.com/hrydgard/ppsspp/issues/14069 "Mini-Map in Z.H.P. Updates Incorrectly Without Software Rendering"
[#13252]: https://github.com/hrydgard/ppsspp/issues/13252 "Open Season Title Screen does not display"
[#14127]: https://github.com/hrydgard/ppsspp/issues/14127 "Metal Gear Solid Peace Walker Chinese Patched blue screen"
[#14271]: https://github.com/hrydgard/ppsspp/issues/14271 "Ape Academy 2 is broken on versions after 1.8.0(?) - tested on latest nightly and 1.11.3"
[#13741]: https://github.com/hrydgard/ppsspp/issues/13741 "Princess Maker 5 Portable half screen in Vulkan"
[#14167]: https://github.com/hrydgard/ppsspp/issues/14167 "[Android] Pro Yakyu Spirits 2010 (NPJH50234): Rendering errors with hardware transform off"
[#14230]: https://github.com/hrydgard/ppsspp/issues/14230 "Support texture replacement filtering overrides"
[#13759]: https://github.com/hrydgard/ppsspp/issues/13759 "Yarudora Portable: Double Cast"
[#13146]: https://github.com/hrydgard/ppsspp/issues/13146 "Sims 2 Castaway/Pets EA Logo glitched out - 1.10.2"
[#14789]: https://github.com/hrydgard/ppsspp/issues/14789 "Add new texture filtering mode \"Auto Max Quality\""
[#14209]: https://github.com/hrydgard/ppsspp/issues/14209 "Fix Size & Position jpn0.pgf/ltn0.pgf/ltn2.pgf/ltn4.pgf/ltn6.pgf"
[#14042]: https://github.com/hrydgard/ppsspp/issues/14042 "Implement basic depth texturing for OpenGL"
[#14966]: https://github.com/hrydgard/ppsspp/issues/14966 "Config: Correct cardboard setting ini load"
[#14768]: https://github.com/hrydgard/ppsspp/issues/14768 "Fix the math in cardboard VR mode for wide aspect ratios"
[#14879]: https://github.com/hrydgard/ppsspp/issues/14879 "vertexjit: Correct morph flag alpha check assert"
[#14313]: https://github.com/hrydgard/ppsspp/issues/14313 "Add a setting for choosing background animation in PPSSPP's menus"
[#14818]: https://github.com/hrydgard/ppsspp/issues/14818 "Focus based moving background"
[#14810]: https://github.com/hrydgard/ppsspp/issues/14810 "Wave animation"
[#14347]: https://github.com/hrydgard/ppsspp/issues/14347 "UI: Add BG animation for recent games"
[#14041]: https://github.com/hrydgard/ppsspp/issues/14041 "UI: Add button to show CRC on feedback screen"
[#14224]: https://github.com/hrydgard/ppsspp/issues/14224 "Add a Storage tab to System Information with some path info"
[#14238]: https://github.com/hrydgard/ppsspp/issues/14238 "UI: Wrap long info items and cleanup storage display"
[#14056]: https://github.com/hrydgard/ppsspp/issues/14056 "Track memory allocations and writes for debug info"
[#14237]: https://github.com/hrydgard/ppsspp/issues/14237 "Add initial search to savedata manager"
[#14528]: https://github.com/hrydgard/ppsspp/issues/14528 "Postshader: Let shaders use the previous frame"
[#14548]: https://github.com/hrydgard/ppsspp/issues/14548 "Add some PPSSPP's Japanese keyboard"
[#14708]: https://github.com/hrydgard/ppsspp/issues/14708 "Add Reset button on crash screen, allow load state and related"
[#14676]: https://github.com/hrydgard/ppsspp/issues/14676 "Add savestate undo UI"
[#14679]: https://github.com/hrydgard/ppsspp/issues/14679 "Savestate load undo"
[#14697]: https://github.com/hrydgard/ppsspp/issues/14697 "Add undo last save as well"
[#14596]: https://github.com/hrydgard/ppsspp/issues/14596 "Replace the \"Test Analogs\" screen with a new screen that lets you directly try the settings."
[#13869]: https://github.com/hrydgard/ppsspp/issues/13869 "Make combo button more generic"
[#12530]: https://github.com/hrydgard/ppsspp/issues/12530 "Allow tilt input on Z instead of X"
[#14000]: https://github.com/hrydgard/ppsspp/issues/14000 "Add CRC32 calc"
[#14769]: https://github.com/hrydgard/ppsspp/issues/14769 "Add a visual means of control mapping"
[#13107]: https://github.com/hrydgard/ppsspp/issues/13107 "Basic mappable motion gesture"
[#14843]: https://github.com/hrydgard/ppsspp/issues/14843 "DPad drag fixes"
[#14480]: https://github.com/hrydgard/ppsspp/issues/14480 "Configurable analog head size"
[#14140]: https://github.com/hrydgard/ppsspp/issues/14140 "[Adhoc] Fix multiplayer issue on MGS:PW due to detecting an incorrect source port on incoming data"
[#14235]: https://github.com/hrydgard/ppsspp/issues/14235 "[Adhoc] Always enable TCPNoDelay to improve response time"
[#14284]: https://github.com/hrydgard/ppsspp/issues/14284 "[Adhoc] Fix Teenage Mutant Ninja Turtles Multiplayer"
[#14290]: https://github.com/hrydgard/ppsspp/issues/14290 "Fix FlatOut Head On multiplayer."
[#14335]: https://github.com/hrydgard/ppsspp/issues/14335 "[Adhoc] Prevent flooding Adhoc Server with connection attempts"
[#14342]: https://github.com/hrydgard/ppsspp/issues/14342 "[AdhocMatching] Fix crashing issue when leaving a multiplayer game room"
[#14345]: https://github.com/hrydgard/ppsspp/issues/14345 "[APctl] Fix stuck issue when scanning AP to Recruit on MGS:PW"
[#14466]: https://github.com/hrydgard/ppsspp/issues/14466 "[Adhoc] Fix possible crash issue on blocking socket implementation."
[#14492]: https://github.com/hrydgard/ppsspp/issues/14492 "[AdhocGameMode] Create GameMode's socket after Master and all Replicas have been created"
[#14513]: https://github.com/hrydgard/ppsspp/issues/14513 "[Adhoc] Reducing HLE delays due to Mutiplayer performance regressions"
[#14849]: https://github.com/hrydgard/ppsspp/issues/14849 "[Adhoc] Fix Socket error 10014 on Windows when hosting a game of Vulcanus Seek and Destroy"
[#14711]: https://github.com/hrydgard/ppsspp/issues/14711 "Sas: Add option to control reverb volume"
[#15237]: https://github.com/hrydgard/ppsspp/issues/15237 "Path: Check for PSP case insensitively"
[#15487]: https://github.com/hrydgard/ppsspp/issues/15487 "Save textures on background tasks when texture dumping is enabled."
[#15645]: https://github.com/hrydgard/ppsspp/issues/15645 "UI: Add analog speed limit mapping"
[#15566]: https://github.com/hrydgard/ppsspp/issues/15566 "Screen upscaling shaders improvements"
[#15163]: https://github.com/hrydgard/ppsspp/issues/15163 "Implement a jit for drawing pixels in the software renderer"
[#15345]: https://github.com/hrydgard/ppsspp/issues/15345 "Fix some minor softgpu blending bugs"
[#15389]: https://github.com/hrydgard/ppsspp/issues/15389 "Draw rectangles always using a specialized path in softgpu"
[#15529]: https://github.com/hrydgard/ppsspp/issues/15529 "softgpu: Fix viewport flag clean/dirty"
[#15440]: https://github.com/hrydgard/ppsspp/issues/15440 "softgpu: Plug bad leak of bin queue data"
[#15410]: https://github.com/hrydgard/ppsspp/issues/15410 "softgpu: Remove offset from screenpos, adjust filtering coords"
[#15405]: https://github.com/hrydgard/ppsspp/issues/15405 "Fix some samplerjit issues without SSE4 or AVX"
[#15400]: https://github.com/hrydgard/ppsspp/issues/15400 "softgpu: Track dirty vs really dirty per buffer"
[#15481]: https://github.com/hrydgard/ppsspp/issues/15481 "Fix some NEON code that had bad compile-time checks"
[#15474]: https://github.com/hrydgard/ppsspp/issues/15474 "Merge CheckAlpha into texture decoding"
[#15238]: https://github.com/hrydgard/ppsspp/issues/15238 "Vulkan: Be more restrictive about hardware texture upscaling on \"slow\" GPUs"
[#15217]: https://github.com/hrydgard/ppsspp/issues/15217 "Vulkan is strict about scissor rect, so let's clamp centrally."
[#15211]: https://github.com/hrydgard/ppsspp/issues/15211 "Vulkan: Specify Vulkan version, fix mip level generation calculation"
[#15162]: https://github.com/hrydgard/ppsspp/issues/15162 "Integrate VMA (Vulkan Memory Allocator)"
[#15106]: https://github.com/hrydgard/ppsspp/issues/15106 "GLES: Explicitly enable ARB_cull_distance"
[#15075]: https://github.com/hrydgard/ppsspp/issues/15075 "Draw points using triangles"
[#15470]: https://github.com/hrydgard/ppsspp/issues/15470 "Threading manager stresstest and fixes"
[#14414]: https://github.com/hrydgard/ppsspp/issues/14414 "Add search for settings"
[#15613]: https://github.com/hrydgard/ppsspp/issues/15613 "Allow to repeat a \"single\" button"
[#15569]: https://github.com/hrydgard/ppsspp/issues/15569 "Upscaling shaders"
[#15494]: https://github.com/hrydgard/ppsspp/issues/15494 "Add key bind to hotswap internal screen rotation"
[#15510]: https://github.com/hrydgard/ppsspp/issues/15510 "Allow to set InternalScreenRotation per game"
[#12958]: https://github.com/hrydgard/ppsspp/issues/12958 "Feature Request: restore software rendering ui setting on android"
[#15016]: https://github.com/hrydgard/ppsspp/issues/15016 "[Android][Mali GPU] Vulkan backend workaround issue in some games with graphics glitch."
[#15640]: https://github.com/hrydgard/ppsspp/issues/15640 "Disable ForceMax60FPS for GOW games and replace it with fixed 60 fps"
[#15213]: https://github.com/hrydgard/ppsspp/issues/15213 "[Adhoc] Updated PdpCreate, PdpSend, PdpRecv, GetPdpStat, GetPtpStat"
[#15215]: https://github.com/hrydgard/ppsspp/issues/15215 "[Adhocctl] Fix Tekken 5 Dark Resurrection Multiplayer"
[#15549]: https://github.com/hrydgard/ppsspp/issues/15549 "GPU: Hook Sol Trigger func to flush texture"
[#15583]: https://github.com/hrydgard/ppsspp/issues/15583 "Fix and further improve line drawing in Echochrome"
[#15073]: https://github.com/hrydgard/ppsspp/issues/15073 "Cleanup line/point handling and refactor a bit"
[#15500]: https://github.com/hrydgard/ppsspp/issues/15500 "Add BlueToAlpha compat.ini workaround, fixes Split/Second graphics"
[#15501]: https://github.com/hrydgard/ppsspp/issues/15501 "Make the existing ReinterpretFramebuffers/ShaderColorBitmask path work for Split/Second"
[#15652]: https://github.com/hrydgard/ppsspp/issues/15652 "Replace Win32 file IO with UWP safe variants and add support for getting drives to UWP build"
[#15396]: https://github.com/hrydgard/ppsspp/issues/15396 "Add UI Tint/Saturation settings"
[#15394]: https://github.com/hrydgard/ppsspp/issues/15394 "Allow custom UI themes"
[#15560]: https://github.com/hrydgard/ppsspp/issues/15560 "UI: Abandon focus movement on returning from pause"
[#15393]: https://github.com/hrydgard/ppsspp/issues/15393 "GE Debugger: Avoid crash on Step Draw with flush"
[#15324]: https://github.com/hrydgard/ppsspp/issues/15324 "UI: Reset ZIP install errors for new ZIPs"
[#15377]: https://github.com/hrydgard/ppsspp/issues/15377 "Debugger: Avoid mem write tag lookup on small alloc"
[#15424]: https://github.com/hrydgard/ppsspp/issues/15424 "Windows: Create SYSTEM directory early"
[#15402]: https://github.com/hrydgard/ppsspp/issues/15402 "GE Debugger: Highlight changed state values"
[#15378]: https://github.com/hrydgard/ppsspp/issues/15378 "GE Debugger: Add filter to skip prim calls"
[#15338]: https://github.com/hrydgard/ppsspp/issues/15338 "Alow flushing at will via the GE debugger"
[#15025]: https://github.com/hrydgard/ppsspp/issues/15025 "Allow delayed loading of texture replacements"
[#15691]: https://github.com/hrydgard/ppsspp/issues/15691 "Add a simple compat flag to workaround the Clone Wars issue, #12949"
[#15696]: https://github.com/hrydgard/ppsspp/issues/15696 "Use the recent Clone Wars fix for Star Wars: Force Unleashed too"
[#12949]: https://github.com/hrydgard/ppsspp/issues/12949 "Star Wars: The Clone Wars - Graphic glitch [Android/Windows]"
[#9572]: https://github.com/hrydgard/ppsspp/issues/9572 "Star Wars force unleashed [Screen Overlay problem]"
[#15687]: https://github.com/hrydgard/ppsspp/issues/15687 "Add Zettai Zetsumei Toshi 3"
[#7295]: https://github.com/hrydgard/ppsspp/issues/7295 "Juiced 2: Hot Import Nights, screen artifacts and missing half of race tracks"
[#15717]: https://github.com/hrydgard/ppsspp/issues/15717 "Allows \"merging\" render targets that overlap on the Y axis. Fixes Juiced 2"
[#15698]: https://github.com/hrydgard/ppsspp/issues/15698 "Osk: Allow upper/lower for all keyboards"