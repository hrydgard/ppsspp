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

What's new in 1.14
==================
* Massive number of rendering fixes
  - Water in Outrun 2006 and DiRT 2 now renders correctly (logic op fixes) ([#15960], [#16208], [#16032], [#16024], [#15967])
  - Split/Second now renders all effects correctly
  - Multiple fixes workarounds for clipping/culling, both through clip planes and geometry shaders, fixing
    graphical issues in many, many games and getting rid of hacks ([#16142], [#16504], [#16442], [#16383], [#16165], [#16162], [#16049], others)
  - Killzone color effect now renders efficiently and correctly (in-game only, not title screen) ([#15934])
  - Ridge Racer speedometers and lens flares fixed ([#16084], [#16188], [#16115])
  - Lens flare effect fixed in Burnout Dominator, Ridge Racer, Colin McRae, several others ([#16014], [#16081], [#16076], [#16073])
  - Shadows in Colin McRae are no longer flickering (side effect of other fixes)
  - Spongebob: Yellow Avenger renders correctly (previously very broken) ([#15907], [#15903])
  - Cars: Race-o-Rama, and MX vs ATV Reflex render correctly (at 1x resolution only) ([#15898], [#15907])
  - Asphalt 2 depth occlusion problems fixed ([#15854], [#15853])
  - Fix performance regression in Juiced 2 while also fixing the graphics ([#15888])
  - Silent Hill games are less broken ([#16127])
  - Depth occlusion and other problems fixed in Kurohyou (both games) and Ratchet & Clank ([#16454], [#15772], [#15728], [#15859])
  - Misshitsu no Sacrifice intro animation and Macross water rendering fixed (3D texture) ([#15727])
  - Tekken 6 Nancy laser beam fixed, plus line rendering fixes in rRootage and other games ([#16067])
  - Tiger & Bunny, Yu-Gi-Oh, GEB, and PlayView games - JPEG image display issues ([#16179], [#16184], [#15924])
  - Many, many others like Hunter x Hunter, Crash: Mind over Mutant, Boundless Trails, etc. ([#16265], [#16043], [#16379], [#15822], [#16358])
* Software renderer performance and accuracy improvements
  - Better performance ([#15998], [#16001], [#16011], [#16039], [#16054], [#16080], [#16085], [#16094], [#16102], [#16387], [#16486], [#16502], [#16518])
  - Improved accuracy, clipping ([#15999], [#16005], [#16042], [#16086], [#16117], [#16231], [#16241], [#16265], [#16274], [#16469], [#16470], [#16478], [#16480], [#16485])
* New features
  - Initial VR support added (Quest and PICO only for now, PC in the future) ([#15659], [#15901], [#16246], [#16262], [#16273])
  - MSAA antialiasing added to the Vulkan backend (desktop only) ([#16458])
  - New API for plugins to access aspect ratio, scaling and fast-forward ([#16441]), other new APIs & improvements ([#15748], [#16121], [#16187], [#16198], [#16389])
  - Read texture replacement packs directly from ZIP files ([#16304])
* UI improvements
  - New interactive Display Layout & Effects screen, replacing the old Display Layout Editor ([#16409], [#16415], [#16417], [#16445])
  - Add default shader for LCD persistence simulation ([#16531])
  - Game being played can now be seen as a background in most menus ([#16404], more)
  - Reorganize speed hack settings ([#16346], [#16347], [#16348], [#16432])
* Stability fixes
  - Workaround for hangs on older Adreno GPUs ([#16422])
  - Input handling fixes for deadzones and touch controls ([#16419], [#16450])
  - Avoid game bugs in Twinbee Portable ([#16388]) and Shining Ark ([#16449])
  - Fixes to D3D9 backend issues ([#15723], [#15815], [#15926], [#16100], [#16232], [#16550])
* IR interpreter (iOS, etc.) bug fixes
  - Metal Gear Solid - Peace Walker no longer bugged out ([#16396])
  - VFPU fixes for Dissidia, others ([#16302], [#16305], [#16306])
* Performance improvements
  - Vulkan bandwidth and synchronization optimizations ([#16434], [#16099], [#16090], [#16072], [#16061], [#16060], [#16035], [#15917])
  - Lighting "ubershader" optimization to prevent hitches ([#16104], [#16111])
  - Assorted minor other improvements ([#15589], [#15843], [#16190])
  - Improve texture replacement memory usage ([#15884], [#16304], [#16314])
  - Texture upscaling speedup and fixes ([#15803], [#16125])
* Other
  - HLE/CPU accuracy improvements helping Brooktown High, Frontier Gate, Madoka Magicka, some language patches ([#16413], [#16070], [#16052], [#15930], [#15952], [#15957], more)
  - Many GE debugger improvements ([#15839], [#15851], [#15894], [#15925], [#15974], [#16007], [#16047], [#16096], [#16201])
  - Optional memory alignment validation in IR mode ([#15879], [#15880])
  - Fix netplay assertion in Cars ([#16089])

What's new in 1.13.2
====================
* Crashfix on Android 12 when playing certain background music ([#15990])
* Fix Star Ocean battles in D3D backends ([#15889])
* Minor fixes that might fix some other crashes

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

Looking for [older news](history.md)?


Adhoc support
-------------
Not fully functional, but some games work.  Check the [Ad-Hoc section of the forum](https://forums.ppsspp.org/forumdisplay.php?fid=34) for help.

Credit goes to:
 - ANR2ME
 - Igor Calabria
 - [coldbird's code](https://code.google.com/archive/p/aemu/)
 - Kyhel
 - And more, of course.


[comment]: # (LINK_LIST_BEGIN_HERE)
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
[#15738]: https://github.com/hrydgard/ppsspp/issues/15738 "Add confirmation dialog when generating a new Mac address"
[#15739]: https://github.com/hrydgard/ppsspp/issues/15739 "irjit: Correct another PurgeTemps case"
[#15740]: https://github.com/hrydgard/ppsspp/issues/15740 "Replacement: Clear cache on disable"
[#15734]: https://github.com/hrydgard/ppsspp/issues/15734 "Better pt-pt translation"
[#15733]: https://github.com/hrydgard/ppsspp/issues/15733 "Fix bug in blue-to-alpha - alpha blending could be on when it shouldn't be."
