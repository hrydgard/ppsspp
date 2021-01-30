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

What's new in 1.11.0
====================
There's really too much to list this time.
* Lots of minor bug fixes, crash fixes, and performance fixes and improvements.
* Graphics issue with car reflections fixed in Outrun, Dirt 2 ([#13636], [#13640], [#13760])
* Countless AdHoc networking fixes by ANR2ME, too many to list
* Crashes fixed in Crazy Taxi ([#13368]), Spiderman: Friend or Foe ([#13969])
* Numerous fixes to the builtin fonts by nassau-tk
* Workaround for rendering bugs with flat shading in iOS 14
* Multiple fixes to the IR interpreter ([#13897], ...)
* New Browse... button to allow opening SD cards on Android 11
* UI: New fullscreen button on desktop platforms, optional navigation sounds
* Audio and multiple hangs fixes in UWP version ([#13792], ...)
* Partial microphone support ([#12336], ...)
* Workaround for wacky action mirroring bug in Hitman Reborn Battle Arena 2 ([#13706], [#13526])
* Added MMPX Vulkan texture upscaling shader ([#13986])
* Autogenerate mipmaps for scaled textures ([#13514]) (Vulkan-only)
* Depth texturing support in Vulkan and D3D11 ([13667], ...)
* Performance fix for Test Drive Unlimited ([#13355], ...)
* Namco Musem hangs fixes ([#13298], ...)
and more...

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
* Ghost in the Shell graphics fixed (JIT inaccuracy with inf*0) ([#12519])
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
* Real support for chinese patched version of Hatsune Miku Project Diva Extend ([#13007])
* Some minor kernel module support ([#13028], [#12225], [#13026], [#13004], [#13038], [#13023])
* Fixed fullscreen toggling with Vulkan in SDL builds ([#11974])

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

Looking for [older news](history.md)?


Adhoc support
-------------
Not fully functional, but some games work.  Check the [Ad-Hoc section of the forum](http://forums.ppsspp.org/forumdisplay.php?fid=34) for help.

Credit goes to:
 - Igor Calabria
 - [coldbird's code](https://code.google.com/archive/p/aemu/)
 - Kyhel


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
