PPSSPP - a fast and portable PSP emulator
=========================================

Created by Henrik Rydgård

Additional code by many contributors, see the Credits screen

Originally released under the GPL 2.0 (and later) in November 2012

Official website: https://www.ppsspp.org/

Discord: https://discord.gg/5NJB6dD

No BIOS file required to play, PPSSPP is an "HLE" emulator.  Default settings balance good compatibility and speed.

To contribute, see [the development page](https://www.ppsspp.org/development.html).  Help testing, investigating, or fixing is always welcome.  See [the list of issues](https://github.com/hrydgard/ppsspp/issues).

For the latest source code, see [our GitHub page](https://github.com/hrydgard/ppsspp).

For documentation of all kinds (usage, reference, development), see the [documentation on the main website](https://www.ppsspp.org/docs)

For build instructions and other development tutorials, see [the wiki](https://github.com/hrydgard/ppsspp/wiki).

If you want to download regularly updated builds for Android, Windows x86 and x64, [visit Orphis' buildbot](https://buildbot.orphis.net/ppsspp/)

For game compatibility, see [community compatibility feedback](https://report.ppsspp.org/games).

What's new in 1.15.4
====================
- Tilt controls: Restore "Low end radius" control ([#17489])
- Android: Restore the ability to choose "display" (hardware scaler) resolution ([#17473])
- Improve the workarounds for the DJ Max games ([#17467])
- Android: Fix running some file formats directly from the Downloads folder ([#17466])
- OpenGL: Restore most of the lost performance on low-end hardware from the shader variant reduction ([#17440, [#17439]])
- Add a simple profiling tool to check CPU usage by the GL backend ([#17475])
- Loader: Fix bug preventing WWE 2009 from starting ([#17435])
- Misc minor fixes and optimizations ([#17442], [#17457], [#17486], [#17446], more)

What's new in 1.15.3
====================
- Assorted minor crash- and other fixes ([#17406], [#17414], [#17415], [#17422])
- Android: Allow launch by content URI (for frontends) ([#17425])
- Control mapping: Fix bugs in mapping UI causing trouble with some controllers ([#17412], [#17420])

What's new in 1.15.2
====================
- Assorted minor crash fixes ([#17401], [#17399])
- Fix Android 12 support in the Android VR build (avoid scoped storage) ([#17398])

What's new in 1.15.1
====================
- Assorted minor crash fixes ([#17374], [#17370], [#17392], [#17394])
- Fix for non-png texture replacements when not listed in textures.ini ([#17380])
- Fix for broken tilt d-pad controls ([#17393])
- Workaround for Vulkan driver bugs on Mali-T8x0 series GPUs ([#17396])

What's new in 1.15
==================
* Stutter caused by shader compilation has been reduced drastically ([#16873])
  - Parallelization: ([#16802], [#16804], [#16809], [#16812])
  - Shader variant reduction: ([#16773], [#16778], [#16770], [#16763])

* Rendering performance and fixes
  - Integer scaling added ([#17224])
  - Post-processing fixes ([#17262])
  - SOCOM overlay glitch removed, night vision fixed ([#17297], [#17317])
  - PowerVR compatibility fixes ([#17232])
  - CLUT fixes ([#17212])
  - ToP - Narikiri Dungeon X: Avoid GPU readback ([#17181], [#17191], [#17192])
  - DTM / Toca: Avoid GPU readback ([#16715])
  - Fixed Dante's Inferno performance regression ([#17032], [#17035])
  - Fix wrong device selection on Poco C40 phones, causing broken UI ([#17027], [#17022])
  - Rainbow Six GPU performance fix ([#16971])
  - Subtitles fixed in The Godfather ([#17298], [#17314])

* Texture replacement improvements
  - Less I/O on the main thread, leading to smoother framerates ([#17078], [#17091], [#17120], [#17134])
  - Support for KTX2 files with UASTC compressed textures added ([#17111] [#17104])
  - Support for DDS files with BC1-7 textures added ([#17083], [#17103], [#17097], [#17096], [#17095])
  - Improve default ini ([#17146])
  - Mipmaps now always used if provided ([#17144])
  - Additional optimizations ([#17139], [#17088])

* Optimizations
  - Software renderer fixes and performance ([#17295], [#17214], [#17028], [#16753], [#16706], [#16690])
  - Vulkan texture upload optimizations ([#17052], [#17122], [#17121], [#17114], [#17011])
  - Depth readback added, fixing lens flares in Syphon Filter (at perf cost..) ([#16907], [#16905])
  - Async readback supported in Vulkan - currently only enabled in Dangan Ronpa ([#16910], [#16916])
  - Lighting shader optimizations ([#16791], [#16787])

* Controls
  - Android tilt control has been overhauled and fixed ([#16889], [#16896])
  - You can now map combinations of buttons to single functions ([#17210], [#17215], [#17228], etc)
  - Custom buttons now support analog inputs ([#16855])

* VR features
  - Top down camera: ([#17098])
  - Head rotation control: ([#16857])
  - More stereo support: ([#16952], [#16953])
  - Other: ([#16826], [#16821])

* Other
  - Windows Dark Mode support ([#16704])
  - GLSL shader compatibility fixes ([#16710], [#16709])
  - GTA math issue on macOS/iOS fixed, playable again ([#16928])
  - More accurate VFPU emulation has been added, though not all enabled yet ([#16984])
  - Debugger features and fixes ([#17270], [#17269], [#17263], [#17260], [#17203], [#17190], [#17042], [#16994], [#16988], [#16818] etc)
  - Rewind savestates no longer slows things down a lot ([#17291])
  - Chat window bugfixes ([#17241])
  - IR Jit fixes - helps iOS when native jit is unavailable ([#17129])
  - Depth-related rendering fixes ([#17055], [#16880])
  - More RISCV support work ([#16976], [#16957], [#16962], [#16832], [#16829])
  - macOS native menu bar ([#16922])
  - Font fixes ([#16859])
  - Rockman 2 audio glitch fix ([#16810], [#16798])
  - UI fixes: Vertical use of space ([#16795]), scrollbars ([#16785]), touchpad scroll on Windows ([#16699])

What's new in 1.14.4
====================
* Multiple shader compatibility fixes for older devices/drivers: ([#16710], [#16709], [#16708])
* A few other minor fixes: ([#16703], [#16706])

What's new in 1.14.3
====================
* Several crash/hang fixes ([#16690], [#16689], [#16683], [#16685], [#16680], [#16697], [#16681], more)
* Minor UI fixes ([#16698], [#16684], [#16674], [#16677])
* Fix confirm/cancel button reversal ([#16692])

What's new in 1.14.2
====================
* Fix Toca/DTM and others (culling) on Mali again ([#16645])
* Fix line rendering bugs in the homebrew Tempest clone Webfest ([#16656])
* Assorted cleanup and bugfixes ([#16673], [#16662], [#16655], [#16644], [#16636], [#16639] etc)

What's new in 1.14.1
====================
* Fix black screen in Vulkan on some older Android devices (Android version 7) ([#16599])
* Fix error message in Medal of Honor ([#16614])
* Various minor bugfixes ([#16617], [#16609], [#16608], [#16615], [#16619])
* Add an option to turn off the new transparent menu background ([#16595])

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
  - HLE/CPU accuracy improvements helping Brooktown High, Frontier Gate, Madoka Magicka, some language patches ([#16413], [#16070], [#16052], [#15952], [#15957], more)
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
[#15960]: https://github.com/hrydgard/ppsspp/issues/15960 "Enable logic op emulation in-shader"
[#16208]: https://github.com/hrydgard/ppsspp/issues/16208 "GPU: Respect stencil state in shader blend"
[#16032]: https://github.com/hrydgard/ppsspp/issues/16032 "Fix the water in Outrun when no logic ops"
[#16024]: https://github.com/hrydgard/ppsspp/issues/16024 "GPU: Fix simulating logicop with blend and shader"
[#15967]: https://github.com/hrydgard/ppsspp/issues/15967 "Fix BlueToAlpha mode, re-enable it for Outrun and DiRT"
[#16142]: https://github.com/hrydgard/ppsspp/issues/16142 "Implement geometry shader for range culling"
[#16504]: https://github.com/hrydgard/ppsspp/issues/16504 "GPU: Use accurate depth for depth range hack"
[#16442]: https://github.com/hrydgard/ppsspp/issues/16442 "Vulkan: Only use geometry shaders with accurate depth"
[#16383]: https://github.com/hrydgard/ppsspp/issues/16383 "GPU: Automatically reduce depth range on == test"
[#16165]: https://github.com/hrydgard/ppsspp/issues/16165 "Vulkan: Clip clamped depth in geometry shader"
[#16162]: https://github.com/hrydgard/ppsspp/issues/16162 "Implement negative Z clipping in geometry shader"
[#16049]: https://github.com/hrydgard/ppsspp/issues/16049 "GPU: Clip depth properly when also clamping"
[#15934]: https://github.com/hrydgard/ppsspp/issues/15934 "Split the main framebuffer in Killzone, to avoid texturing-from-current-rendertarget"
[#16084]: https://github.com/hrydgard/ppsspp/issues/16084 "Fix Ridge Racer lens flares - ignore stride if texHeight == 1, when matching tex/fb"
[#16188]: https://github.com/hrydgard/ppsspp/issues/16188 "Fix depal bounds with dynamic CLUT. Fixes lens flare glitches in Ridge Racer"
[#16115]: https://github.com/hrydgard/ppsspp/issues/16115 "Add compatibility flag for loading pixels on framebuffer create using nearest filtering"
[#16014]: https://github.com/hrydgard/ppsspp/issues/16014 "Texture depal using CLUT loaded from framebuffers, and more. Fixes Burnout Dominator lens flare"
[#16081]: https://github.com/hrydgard/ppsspp/issues/16081 "Fix green flashes with Burnout Dominator lens flare"
[#16076]: https://github.com/hrydgard/ppsspp/issues/16076 "Don't try to replace or scale CLUT8-on-GPU textures."
[#16073]: https://github.com/hrydgard/ppsspp/issues/16073 "CLUTs can be loaded from small rectangular textures. Need to linearize."
[#15907]: https://github.com/hrydgard/ppsspp/issues/15907 "Reinterpret between 32 and 16 bit texture formats"
[#15903]: https://github.com/hrydgard/ppsspp/issues/15903 "Remove support for framebuffers changing stride (already unreachable)."
[#15898]: https://github.com/hrydgard/ppsspp/issues/15898 "Rendering issues in Tantalus Media games (Spongebob, MX ATV, etc)"
[#15854]: https://github.com/hrydgard/ppsspp/issues/15854 "Use sequence numbers instead of a tracking array for depth buffers"
[#15853]: https://github.com/hrydgard/ppsspp/issues/15853 "Framebuffer-bind sequence numbers"
[#15888]: https://github.com/hrydgard/ppsspp/issues/15888 "Copy color from overlapping framebuffers on bind, under certain conditions"
[#16127]: https://github.com/hrydgard/ppsspp/issues/16127 "Fixes for Silent Hill: Origins (depth buffer reassignment, eliminate readback)"
[#16454]: https://github.com/hrydgard/ppsspp/issues/16454 "GPU: Support framebuf depal from rendered CLUT"
[#15772]: https://github.com/hrydgard/ppsspp/issues/15772 "Add support for binding the depth buffer as a color target. Fixes Kurohyo depth sorting"
[#15728]: https://github.com/hrydgard/ppsspp/issues/15728 "Better ways to deal with overlapping render targets"
[#15859]: https://github.com/hrydgard/ppsspp/issues/15859 "Allow binding depth as 565"
[#15727]: https://github.com/hrydgard/ppsspp/issues/15727 "Implement the PSP's equal-size mips \"3D texturing\""
[#16067]: https://github.com/hrydgard/ppsspp/issues/16067 "GPU: Account for w properly in lines, fixing width"
[#16179]: https://github.com/hrydgard/ppsspp/issues/16179 "Correct size and YUV order for jpeg decoding"
[#16184]: https://github.com/hrydgard/ppsspp/issues/16184 "GPU: Hook Gods Eater Burst avatar read"
[#15924]: https://github.com/hrydgard/ppsspp/issues/15924 "Add support for reading depth buffers to the PackFramebufferSync function"
[#16265]: https://github.com/hrydgard/ppsspp/issues/16265 "GPU: Respect world matrix and reverse flag w/o normals"
[#16043]: https://github.com/hrydgard/ppsspp/issues/16043 "Consider the Adreno and Mali stencil-discard bugs the same."
[#16379]: https://github.com/hrydgard/ppsspp/issues/16379 "Fix alpha/stencil replace on Adreno when color masked"
[#15822]: https://github.com/hrydgard/ppsspp/issues/15822 "GPU: Write stencil fail to alpha is RGB masked"
[#16358]: https://github.com/hrydgard/ppsspp/issues/16358 "TexCache: Fix 16->32 colors with CLUT start pos"
[#15998]: https://github.com/hrydgard/ppsspp/issues/15998 "softgpu: Allow almost flat rectangles to go fast"
[#16001]: https://github.com/hrydgard/ppsspp/issues/16001 "softgpu: Check depth test early on simple stencil"
[#16011]: https://github.com/hrydgard/ppsspp/issues/16011 "Detect more triangles as rectangles in softgpu"
[#16039]: https://github.com/hrydgard/ppsspp/issues/16039 "softgpu: Run early Z tests in fast rect path"
[#16054]: https://github.com/hrydgard/ppsspp/issues/16054 "softgpu: Reduce some flushing / flushing cost"
[#16080]: https://github.com/hrydgard/ppsspp/issues/16080 "softgpu: Avoid unnecessary flushing for curves"
[#16085]: https://github.com/hrydgard/ppsspp/issues/16085 "softgpu: Cache reused indexed verts"
[#16094]: https://github.com/hrydgard/ppsspp/issues/16094 "softgpu: Optimize rectangle sampling/blending used in bloom"
[#16102]: https://github.com/hrydgard/ppsspp/issues/16102 "softgpu: Avoid waiting for a thread to drain"
[#16387]: https://github.com/hrydgard/ppsspp/issues/16387 "softgpu: Use threads on self-render if safe"
[#16486]: https://github.com/hrydgard/ppsspp/issues/16486 "softgpu: Apply optimizations to states generically"
[#16502]: https://github.com/hrydgard/ppsspp/issues/16502 "A few more softgpu optimizations for alpha blend/test"
[#16518]: https://github.com/hrydgard/ppsspp/issues/16518 "softgpu: Expand fast path to all fb formats"
[#15999]: https://github.com/hrydgard/ppsspp/issues/15999 "softgpu: Clamp/wrap textures at 512 pixels"
[#16005]: https://github.com/hrydgard/ppsspp/issues/16005 "softgpu: Correct accuracy of fog calculation"
[#16042]: https://github.com/hrydgard/ppsspp/issues/16042 "softgpu: Refactor imm prim handling to support fog/color1"
[#16086]: https://github.com/hrydgard/ppsspp/issues/16086 "softgpu: Fix self-render detect in Ridge Racer"
[#16117]: https://github.com/hrydgard/ppsspp/issues/16117 "Correct texture projection issues, mainly in softgpu"
[#16231]: https://github.com/hrydgard/ppsspp/issues/16231 "softgpu: Cull a triangle with all negative w"
[#16241]: https://github.com/hrydgard/ppsspp/issues/16241 "softgpu: Correct linear interp for uneven positions"
[#16274]: https://github.com/hrydgard/ppsspp/issues/16274 "Correct accuracy of bounding box test"
[#16469]: https://github.com/hrydgard/ppsspp/issues/16469 "Correct block transfer overlap and wrapping behavior"
[#16470]: https://github.com/hrydgard/ppsspp/issues/16470 "softgpu: Correctly fix inversions, matching tests"
[#16478]: https://github.com/hrydgard/ppsspp/issues/16478 "softgpu: Interpolate Z for 3D lines"
[#16480]: https://github.com/hrydgard/ppsspp/issues/16480 "softgpu: Cull verts outside post-viewport Z"
[#16485]: https://github.com/hrydgard/ppsspp/issues/16485 "softgpu: Handle infnan fog coefficients better"
[#15659]: https://github.com/hrydgard/ppsspp/issues/15659 "Oculus Quest native support"
[#15901]: https://github.com/hrydgard/ppsspp/issues/15901 "OpenXR - Stereoscopic rendering"
[#16246]: https://github.com/hrydgard/ppsspp/issues/16246 "VR: Add the VR code to all builds. Remove IsVRBuild calls from the renderer."
[#16262]: https://github.com/hrydgard/ppsspp/issues/16262 "OpenXR - Add an option to adjust camera distance"
[#16273]: https://github.com/hrydgard/ppsspp/issues/16273 "Vulkan multiview rendering"
[#16458]: https://github.com/hrydgard/ppsspp/issues/16458 "Implement MSAA support for desktop GPUs in Vulkan"
[#16441]: https://github.com/hrydgard/ppsspp/issues/16441 "Exposed more emulator things to devctl api"
[#15748]: https://github.com/hrydgard/ppsspp/issues/15748 "Windows: Add a simple window message to get the base pointer."
[#16121]: https://github.com/hrydgard/ppsspp/issues/16121 "Debugger: Add API to scan memory for funcs"
[#16187]: https://github.com/hrydgard/ppsspp/issues/16187 "Remote API: hle.func.removeRange added"
[#16198]: https://github.com/hrydgard/ppsspp/issues/16198 "Readback stencil buffer for debugger on GLES"
[#16389]: https://github.com/hrydgard/ppsspp/issues/16389 "Make breakpoints work better in interpreter"
[#16304]: https://github.com/hrydgard/ppsspp/issues/16304 "Improve texture replacement cache and allow read from zip"
[#16409]: https://github.com/hrydgard/ppsspp/issues/16409 "Preserve framebuffer on pause screen even if render resolution is changed"
[#16415]: https://github.com/hrydgard/ppsspp/issues/16415 "Display layout editor - Remove editing widget, just use the background directly"
[#16417]: https://github.com/hrydgard/ppsspp/issues/16417 "Move post processing settings to the Display Layout Editor"
[#16445]: https://github.com/hrydgard/ppsspp/issues/16445 "New screen size controls on Display Layout & Effects screen"
[#16531]: https://github.com/hrydgard/ppsspp/issues/16531 "iota97's \"Motion blur\" - LCD persistence shader, plus fixes to make it work with OpenGL"
[#16404]: https://github.com/hrydgard/ppsspp/issues/16404 "Make the pause screen \"transparent\""
[#16346]: https://github.com/hrydgard/ppsspp/issues/16346 "Change the \"Retain changed textures\" option into a compat.ini option."
[#16347]: https://github.com/hrydgard/ppsspp/issues/16347 "Always skin in decode for software transform and rendering"
[#16348]: https://github.com/hrydgard/ppsspp/issues/16348 "Speed hack setting reorganization"
[#16432]: https://github.com/hrydgard/ppsspp/issues/16432 "Cleanup graphics settings list"
[#16422]: https://github.com/hrydgard/ppsspp/issues/16422 "Add compat flag / bug check for games on old Adreno/GL affected"
[#16419]: https://github.com/hrydgard/ppsspp/issues/16419 "Stick input: Fix issue where deadzone noise from one device could drown out signal from another."
[#16450]: https://github.com/hrydgard/ppsspp/issues/16450 "UI: Fix right analog with single button"
[#16388]: https://github.com/hrydgard/ppsspp/issues/16388 "Twinbee Portable: Add compat flag to avoid game bug with some languages"
[#16449]: https://github.com/hrydgard/ppsspp/issues/16449 "Blind workaround for Shining Ark circle button problem"
[#15723]: https://github.com/hrydgard/ppsspp/issues/15723 "D3D9 state cache cleanup"
[#15815]: https://github.com/hrydgard/ppsspp/issues/15815 "Depth blit using raster"
[#15926]: https://github.com/hrydgard/ppsspp/issues/15926 "Implement shader blending for D3D9"
[#16100]: https://github.com/hrydgard/ppsspp/issues/16100 "D3D9: Allow INTZ depth buffers more correctly"
[#16232]: https://github.com/hrydgard/ppsspp/issues/16232 "D3D9: Correct scissor state cache in Draw"
[#16550]: https://github.com/hrydgard/ppsspp/issues/16550 "Hide the D3D9 option on Intel Xe graphics."
[#16396]: https://github.com/hrydgard/ppsspp/issues/16396 "Correct misbehavior on uninitialized values in IR"
[#16302]: https://github.com/hrydgard/ppsspp/issues/16302 "Handle vrot overlap and vscl/vmscl prefixes more accurately"
[#16305]: https://github.com/hrydgard/ppsspp/issues/16305 "irjit: Fix unordered float compares"
[#16306]: https://github.com/hrydgard/ppsspp/issues/16306 "irjit: Correct prefix validation"
[#16434]: https://github.com/hrydgard/ppsspp/issues/16434 "Vulkan: Use stencil export when available"
[#16099]: https://github.com/hrydgard/ppsspp/issues/16099 "Vulkan: Avoid allocating depth images for stuff like temp copies, depal buffers etc."
[#16090]: https://github.com/hrydgard/ppsspp/issues/16090 "Simplify synchronization in VulkanRenderManager"
[#16072]: https://github.com/hrydgard/ppsspp/issues/16072 "Vulkan: Don't have renderpasses store/load depth buffers when we don't use them"
[#16061]: https://github.com/hrydgard/ppsspp/issues/16061 "Vulkan: Submit main command buffer before acquiring the swapchain image"
[#16060]: https://github.com/hrydgard/ppsspp/issues/16060 "Vulkan FrameData refactor"
[#16035]: https://github.com/hrydgard/ppsspp/issues/16035 "Vulkan: \"Acquire\" the image from the swapchain as late as possible in the frame"
[#15917]: https://github.com/hrydgard/ppsspp/issues/15917 "Vulkan bandwidth optimizations (configure renderpass load/store better)"
[#16104]: https://github.com/hrydgard/ppsspp/issues/16104 "Generate \"Ubershaders\" that can handle all lighting configurations"
[#16111]: https://github.com/hrydgard/ppsspp/issues/16111 "Always do the vertex shader part of the fog computation."
[#15589]: https://github.com/hrydgard/ppsspp/issues/15589 "Vulkan: Parallelize GLSL compilation"
[#15843]: https://github.com/hrydgard/ppsspp/issues/15843 "GPU: Skip fb create upload when clearing"
[#16190]: https://github.com/hrydgard/ppsspp/issues/16190 "Reduce IO primarily during save operations"
[#15884]: https://github.com/hrydgard/ppsspp/issues/15884 "Replacement: Read files only within time budget"
[#16314]: https://github.com/hrydgard/ppsspp/issues/16314 "UI: Install textures as a zip if supported"
[#15803]: https://github.com/hrydgard/ppsspp/issues/15803 "Reimplement bicubic upscaling."
[#16125]: https://github.com/hrydgard/ppsspp/issues/16125 "Remove alpha ignore in xbrz texture shaders."
[#16413]: https://github.com/hrydgard/ppsspp/issues/16413 "Kernel: Respect partition param in heap funcs"
[#16070]: https://github.com/hrydgard/ppsspp/issues/16070 "Kernel: Match index lookup behavior for tls"
[#16052]: https://github.com/hrydgard/ppsspp/issues/16052 "HLE: sceKernelAllocPartitionMemory volatile memory support (partition 5)"
[#15952]: https://github.com/hrydgard/ppsspp/issues/15952 "interp: Handle jumps in branch delay slots better"
[#15957]: https://github.com/hrydgard/ppsspp/issues/15957 "Handle branch/jump in branch delay slots more accurately"
[#15839]: https://github.com/hrydgard/ppsspp/issues/15839 "GE debugger: Allow displaying two tabs at once, separate DL view"
[#15851]: https://github.com/hrydgard/ppsspp/issues/15851 "After recording a GE dump, open an explorer window pointing at the file"
[#15894]: https://github.com/hrydgard/ppsspp/issues/15894 "GE Debugger: Record only one flip if display framebuf not changed, step on vsync"
[#15925]: https://github.com/hrydgard/ppsspp/issues/15925 "GE Debugger: Improve display list disasm"
[#15974]: https://github.com/hrydgard/ppsspp/issues/15974 "Add breakpoint conditions to GE debugger"
[#16007]: https://github.com/hrydgard/ppsspp/issues/16007 "GE Debugger: Add fields to register expressions"
[#16047]: https://github.com/hrydgard/ppsspp/issues/16047 "GE Debugger: Allow search"
[#16096]: https://github.com/hrydgard/ppsspp/issues/16096 "GE Debugger: Add option to track pixel in preview"
[#16201]: https://github.com/hrydgard/ppsspp/issues/16201 "GE Debugger: Normalize framebuffer texture preview"
[#15879]: https://github.com/hrydgard/ppsspp/issues/15879 "irjit: Validate alignment in slow memory mode"
[#15880]: https://github.com/hrydgard/ppsspp/issues/15880 "Core: Show exception on misaligned jump"
[#16089]: https://github.com/hrydgard/ppsspp/issues/16089 "[AdhocMatching] Fix assertion issue when playing Cars over public adhoc server."
[#15990]: https://github.com/hrydgard/ppsspp/issues/15990 "Atrac3+: Allocate some extra"
[#15889]: https://github.com/hrydgard/ppsspp/issues/15889 "Correct D3D viewport offset sign in sw transform"
[#16599]: https://github.com/hrydgard/ppsspp/issues/16599 "Vulkan: Remove the new 0th descriptor set, move everything else back to desc set 0"
[#16614]: https://github.com/hrydgard/ppsspp/issues/16614 "GPU: Keep prevPrim_ set on flush"
[#16617]: https://github.com/hrydgard/ppsspp/issues/16617 "GE Debugger: Prevent double init"
[#16609]: https://github.com/hrydgard/ppsspp/issues/16609 "OpenXR - Rendering fixes for a few games"
[#16608]: https://github.com/hrydgard/ppsspp/issues/16608 "Cleanup value corrections in config load/save"
[#16615]: https://github.com/hrydgard/ppsspp/issues/16615 "D3D9: Support old-style user clip planes"
[#16619]: https://github.com/hrydgard/ppsspp/issues/16619 "Debugger: Don't hang memory dump if stepping in GE"
[#16595]: https://github.com/hrydgard/ppsspp/issues/16595 "Transparent background option"
[#16645]: https://github.com/hrydgard/ppsspp/issues/16645 "Fix vertex shader range culling - the driver bug check was wrong."
[#16656]: https://github.com/hrydgard/ppsspp/issues/16656 "Fix rendering of lines with the same x/y but different z."
[#16673]: https://github.com/hrydgard/ppsspp/issues/16673 "Show bluescreen properly on memory errors that we failed to ignore."
[#16662]: https://github.com/hrydgard/ppsspp/issues/16662 "Correct some reversed dependencies, minor other cleanup"
[#16655]: https://github.com/hrydgard/ppsspp/issues/16655 "OpenXR - Force flat mode for Madden NFL games"
[#16644]: https://github.com/hrydgard/ppsspp/issues/16644 "Additional Android cleanup"
[#16636]: https://github.com/hrydgard/ppsspp/issues/16636 "Crash: Ensure we never handle faults in faults"
[#16639]: https://github.com/hrydgard/ppsspp/issues/16639 "Minor initialization cleanup, setup for Vulkan validation layers on Android"
[#16690]: https://github.com/hrydgard/ppsspp/issues/16690 "softgpu: Detect binner alloc fail and bail"
[#16689]: https://github.com/hrydgard/ppsspp/issues/16689 "Vulkan: Avoid race in compile thread exit"
[#16683]: https://github.com/hrydgard/ppsspp/issues/16683 "Assorted fixes after looking at crash data"
[#16685]: https://github.com/hrydgard/ppsspp/issues/16685 "Replacement: Verify out stride"
[#16680]: https://github.com/hrydgard/ppsspp/issues/16680 "Make sure we don't multithread libzip access"
[#16697]: https://github.com/hrydgard/ppsspp/issues/16697 "Check for valid memory range when doing fast bone matrix loads"
[#16681]: https://github.com/hrydgard/ppsspp/issues/16681 "Cleanup some mic data reading"
[#16698]: https://github.com/hrydgard/ppsspp/issues/16698 "Screen background fixes"
[#16684]: https://github.com/hrydgard/ppsspp/issues/16684 "Enabled CPU breakpoints unchecked"
[#16674]: https://github.com/hrydgard/ppsspp/issues/16674 "Broken menus after using Break on Load"
[#16677]: https://github.com/hrydgard/ppsspp/issues/16677 "D3D11: Fix Draw state issues on pause screen"
[#16692]: https://github.com/hrydgard/ppsspp/issues/16692 "Dialog: Fix confirm/cancel button reversal"
[#16710]: https://github.com/hrydgard/ppsspp/issues/16710 "GLES: Use uint for uint shift amounts"
[#16709]: https://github.com/hrydgard/ppsspp/issues/16709 "Correct some shader errors in reporting"
[#16708]: https://github.com/hrydgard/ppsspp/issues/16708 "Additional fixes from both our reports and Play reports"
[#16703]: https://github.com/hrydgard/ppsspp/issues/16703 "Minor fixes based on the latest Google Play report"
[#16706]: https://github.com/hrydgard/ppsspp/issues/16706 "softgpu: Fix lighting with 0 exp"
[#16873]: https://github.com/hrydgard/ppsspp/issues/16873 "Add facility to run tasks on dedicated threads using the ThreadManager interface"
[#16802]: https://github.com/hrydgard/ppsspp/issues/16802 "Vulkan: Parallel pipeline creation"
[#16804]: https://github.com/hrydgard/ppsspp/issues/16804 "Fix a race condition during Vulkan shader cache load."
[#16809]: https://github.com/hrydgard/ppsspp/issues/16809 "ThreadManager: Don't allow reordering of queue"
[#16812]: https://github.com/hrydgard/ppsspp/issues/16812 "ThreadManager: Add simple priority queues"
[#16773]: https://github.com/hrydgard/ppsspp/issues/16773 "Remove the FS_TEXTURE_AT_OFFSET fragment shader flag"
[#16778]: https://github.com/hrydgard/ppsspp/issues/16778 "Remove the rather redundant DoTexture flag from vshaders."
[#16770]: https://github.com/hrydgard/ppsspp/issues/16770 "Shader generator: Switch the 2x flag to a uniform"
[#16763]: https://github.com/hrydgard/ppsspp/issues/16763 "Shader generator: Move FS_TEX_ALPHA to a uniform bool."
[#17224]: https://github.com/hrydgard/ppsspp/issues/17224 "Add support for integer scale factor for display"
[#17262]: https://github.com/hrydgard/ppsspp/issues/17262 "Fix issue in present where we applied the UV range at the wrong place when post-processing"
[#17297]: https://github.com/hrydgard/ppsspp/issues/17297 "Workaround for some SOCOM games' misuse of CLUT8 to texture from framebuffer"
[#17317]: https://github.com/hrydgard/ppsspp/issues/17317 "Fix night vision in SOCOM games (in fact, fix the CLUT8 effect properly)"
[#17232]: https://github.com/hrydgard/ppsspp/issues/17232 "Don't use inaccurate depth with Vulkan on any GPU"
[#17212]: https://github.com/hrydgard/ppsspp/issues/17212 "More accurate check for LoadCLUT from framebuffer margins."
[#17181]: https://github.com/hrydgard/ppsspp/issues/17181 "Tales of Phantasia - Narikiri Dungeon X: Avoid some GPU readbacks."
[#17191]: https://github.com/hrydgard/ppsspp/issues/17191 "GPU: Fix intra-block transfers in ToP CE"
[#17192]: https://github.com/hrydgard/ppsspp/issues/17192 "GPU: Add xfer flag to ignore create vfb flags"
[#16715]: https://github.com/hrydgard/ppsspp/issues/16715 "Eliminate GPU readbacks in the DTM Race Driver series."
[#17032]: https://github.com/hrydgard/ppsspp/issues/17032 "Cache framebuffer copies (for self-texturing) until the next TexFlush GPU instruction"
[#17035]: https://github.com/hrydgard/ppsspp/issues/17035 "GPU: Discard framebuffer copy when clearing"
[#17027]: https://github.com/hrydgard/ppsspp/issues/17027 "Vulkan on Android: In non-debug mode, avoid devices that were rejected"
[#17022]: https://github.com/hrydgard/ppsspp/issues/17022 "Android: Make font rendering work even absent support for R4G4B4A4 textures"
[#16971]: https://github.com/hrydgard/ppsspp/issues/16971 "Add a heuristic avoiding joining framebuffers horizontally..."
[#17298]: https://github.com/hrydgard/ppsspp/issues/17298 "Don't try to present from little temp framebuffers used"
[#17314]: https://github.com/hrydgard/ppsspp/issues/17314 "GPU: Always update size when shrinking framebuffers"
[#17078]: https://github.com/hrydgard/ppsspp/issues/17078 "Texture replacer: Make the internal cache model texture-centric instead of miplevel-centric"
[#17091]: https://github.com/hrydgard/ppsspp/issues/17091 "Replacement: Do all I/O on threaded tasks"
[#17120]: https://github.com/hrydgard/ppsspp/issues/17120 "More texture replacement fixes"
[#17134]: https://github.com/hrydgard/ppsspp/issues/17134 "Refactor the replacement cache"
[#17111]: https://github.com/hrydgard/ppsspp/issues/17111 "Basis/UASTC texture compression support via ktx2"
[#17104]: https://github.com/hrydgard/ppsspp/issues/17104 "basis_universal support: Add the library"
[#17083]: https://github.com/hrydgard/ppsspp/issues/17083 "DDS texture support in texture replacer"
[#17103]: https://github.com/hrydgard/ppsspp/issues/17103 "Texture replacement: Load DDS mipmaps"
[#17097]: https://github.com/hrydgard/ppsspp/issues/17097 "Texture Replacement: Support compressed textures in D3D9 as well"
[#17096]: https://github.com/hrydgard/ppsspp/issues/17096 "Change BGRA to be a texture-specific flag. Fixes R/B swap in DDS textures in D3D11."
[#17095]: https://github.com/hrydgard/ppsspp/issues/17095 "More tex replacement work"
[#17146]: https://github.com/hrydgard/ppsspp/issues/17146 "Improve default replacer ini"
[#17144]: https://github.com/hrydgard/ppsspp/issues/17144 "Force mipmapping on when drawing using replacement textures that contain mipmaps"
[#17139]: https://github.com/hrydgard/ppsspp/issues/17139 "Texture replacement: Improve padding support"
[#17088]: https://github.com/hrydgard/ppsspp/issues/17088 "Replacer: Avoid tracking video textures"
[#17295]: https://github.com/hrydgard/ppsspp/issues/17295 "Optimize lighting for softgpu a bit"
[#17214]: https://github.com/hrydgard/ppsspp/issues/17214 "softgpu: Fix over-optimization of alpha test"
[#17028]: https://github.com/hrydgard/ppsspp/issues/17028 "Fix crash in SoftGPU when frameskipping, noticed"
[#16753]: https://github.com/hrydgard/ppsspp/issues/16753 "Use NEON intrinsics in software renderer"
[#17052]: https://github.com/hrydgard/ppsspp/issues/17052 "Vulkan texture uploads: Take optimalBufferCopyRowPitchAlignment into account"
[#17122]: https://github.com/hrydgard/ppsspp/issues/17122 "VulkanPushPool - more efficient replacement for 3x VulkanPushBuffer"
[#17121]: https://github.com/hrydgard/ppsspp/issues/17121 "Remove an unused VulkanPushBuffer."
[#17114]: https://github.com/hrydgard/ppsspp/issues/17114 "Vulkan: During texture upload, batch the buffer->image copies to do all the mips at once."
[#17011]: https://github.com/hrydgard/ppsspp/issues/17011 "Resurrect the Vulkan memory visualizer, but now it's global stats and pushbuffer stats."
[#16907]: https://github.com/hrydgard/ppsspp/issues/16907 "Fix Syphon Filter lens flares"
[#16905]: https://github.com/hrydgard/ppsspp/issues/16905 "Depth readback with built-in stretchblit"
[#16910]: https://github.com/hrydgard/ppsspp/issues/16910 "Prepare for adding async readback (use VMA for readback allocs, add a param)"
[#16916]: https://github.com/hrydgard/ppsspp/issues/16916 "Implement delayed depth readbacks, Vulkan only"
[#16791]: https://github.com/hrydgard/ppsspp/issues/16791 "Lighting code cleanup and optimization"
[#16787]: https://github.com/hrydgard/ppsspp/issues/16787 "Vertex shaders: On platforms with uniform buffers, use indexing and loop over the lights."
[#16889]: https://github.com/hrydgard/ppsspp/issues/16889 "Tilt improvements: Add visualizer, better defaults"
[#16896]: https://github.com/hrydgard/ppsspp/issues/16896 "Tilt improvements 2: Fix/overhaul calibration, add more previews"
[#17210]: https://github.com/hrydgard/ppsspp/issues/17210 "ControlMapper refactoring"
[#17215]: https://github.com/hrydgard/ppsspp/issues/17215 "Control map multiple keys to one output"
[#17228]: https://github.com/hrydgard/ppsspp/issues/17228 "Add back our older VFPU approximations, as fallbacks if the table files are missing"
[#16855]: https://github.com/hrydgard/ppsspp/issues/16855 "Add analog to custom button and gesture"
[#17098]: https://github.com/hrydgard/ppsspp/issues/17098 "OpenXR - Enable user to switch between topdown and fps camera"
[#16857]: https://github.com/hrydgard/ppsspp/issues/16857 "OpenXR - Control game camera using head rotation"
[#16952]: https://github.com/hrydgard/ppsspp/issues/16952 "OpenXR - Enable stereo in more games"
[#16953]: https://github.com/hrydgard/ppsspp/issues/16953 "Stereo rendering minor UI fix"
[#16826]: https://github.com/hrydgard/ppsspp/issues/16826 "OpenXR - Fix axis mirroing for Tales of the World"
[#16821]: https://github.com/hrydgard/ppsspp/issues/16821 "OpenXR - Enable level 5 CPU/GPU performance on Quest 2"
[#16704]: https://github.com/hrydgard/ppsspp/issues/16704 "Windows Dark Mode: initial support"
[#16928]: https://github.com/hrydgard/ppsspp/issues/16928 "Workaround for sin/cos issue in GTA on Mac (and maybe others)"
[#16984]: https://github.com/hrydgard/ppsspp/issues/16984 "VFPU sin/cos"
[#17270]: https://github.com/hrydgard/ppsspp/issues/17270 "Debugger: Add memory breakpoint conditions"
[#17269]: https://github.com/hrydgard/ppsspp/issues/17269 "Debugger: Avoid unaligned reads in expressions"
[#17263]: https://github.com/hrydgard/ppsspp/issues/17263 "Debugger: sceKernelPrintf improvement, QOL adjustments"
[#17260]: https://github.com/hrydgard/ppsspp/issues/17260 "Debugger: Accept format for watches and stack walk tweak"
[#17203]: https://github.com/hrydgard/ppsspp/issues/17203 "Added new option \"Copy Float (32 bit)\" to Windows Debugger UI"
[#17190]: https://github.com/hrydgard/ppsspp/issues/17190 "Debugger: Update symbols properly on prx load"
[#17042]: https://github.com/hrydgard/ppsspp/issues/17042 "Implement requested debugger features"
[#16994]: https://github.com/hrydgard/ppsspp/issues/16994 "Debugger: copy PSP memory base to clipboard"
[#16988]: https://github.com/hrydgard/ppsspp/issues/16988 "Debugger: Lock memory during stack walk"
[#16818]: https://github.com/hrydgard/ppsspp/issues/16818 "Fix a few warnings and a debugger emuhack bug on mem access"
[#17291]: https://github.com/hrydgard/ppsspp/issues/17291 "Apply the fix to avoid jit clearing for rewind savestates to all platforms"
[#17241]: https://github.com/hrydgard/ppsspp/issues/17241 "Chat window fixes"
[#17129]: https://github.com/hrydgard/ppsspp/issues/17129 "irjit: Fix vi2us/vi2s with non-consecutive"
[#17055]: https://github.com/hrydgard/ppsspp/issues/17055 "GPU: Correct depth clip/cull for zero scale"
[#16880]: https://github.com/hrydgard/ppsspp/issues/16880 "GPU: Allow depth above 65535"
[#16976]: https://github.com/hrydgard/ppsspp/issues/16976 "riscv: Implement skinning in vertexjit"
[#16957]: https://github.com/hrydgard/ppsspp/issues/16957 "riscv: Initial vertexjit"
[#16962]: https://github.com/hrydgard/ppsspp/issues/16962 "riscv: Correct offset prescale in vertexjit"
[#16832]: https://github.com/hrydgard/ppsspp/issues/16832 "riscv: Add bitmanip instructions to emitter"
[#16829]: https://github.com/hrydgard/ppsspp/issues/16829 "Add vector instructions to RISC-V emitter"
[#16922]: https://github.com/hrydgard/ppsspp/issues/16922 "macOS native bar button items"
[#16859]: https://github.com/hrydgard/ppsspp/issues/16859 "Fix for issue of disappear text on Shinobido Talese of The Ninja"
[#16810]: https://github.com/hrydgard/ppsspp/issues/16810 "Sas: Adjust Rockman 2 sustain on init only"
[#16798]: https://github.com/hrydgard/ppsspp/issues/16798 "Add workaround for hung music notes in Rockman Dash 2"
[#16795]: https://github.com/hrydgard/ppsspp/issues/16795 "Improve the use of space on the main screen in vertical mode."
[#16785]: https://github.com/hrydgard/ppsspp/issues/16785 "UI: Make vertical scrollbars directly draggable"
[#16699]: https://github.com/hrydgard/ppsspp/issues/16699 "Fix smooth touchpad scrolling on Windows"
[#17374]: https://github.com/hrydgard/ppsspp/issues/17374 "Assorted crash fixes and asserts"
[#17370]: https://github.com/hrydgard/ppsspp/issues/17370 "Fix ScrollView crash (though the root cause is a race condition most likely)"
[#17392]: https://github.com/hrydgard/ppsspp/issues/17392 "Misc crash fixes from mystery thread"
[#17394]: https://github.com/hrydgard/ppsspp/issues/17394 "Bump the index/vertex cpu-side buffer sizes a little."
[#17380]: https://github.com/hrydgard/ppsspp/issues/17380 "Pre-scan the root of texture packs for hash-named files."
[#17393]: https://github.com/hrydgard/ppsspp/issues/17393 "Prevent tilt-controlled left/right dpad butons from getting stuck"
[#17396]: https://github.com/hrydgard/ppsspp/issues/17396 "Vulkan: Turn off the ubershader on Mali T880, T860 and T830 on old driver versions"
[#17401]: https://github.com/hrydgard/ppsspp/issues/17401 "Additional crash fixes"
[#17399]: https://github.com/hrydgard/ppsspp/issues/17399 "More crash fixes"
[#17398]: https://github.com/hrydgard/ppsspp/issues/17398 "OpenXR - Cleanup unsupported features, support Android 12"[#17406]: https://github.com/hrydgard/ppsspp/issues/17406 "Even more crash fixes"
[#17414]: https://github.com/hrydgard/ppsspp/issues/17414 "GPU: Remove JumpFast/CallFast"
[#17415]: https://github.com/hrydgard/ppsspp/issues/17415 "Misc fixes and checks"
[#17422]: https://github.com/hrydgard/ppsspp/issues/17422 "Windows: Fix initial window show for all displays"
[#17425]: https://github.com/hrydgard/ppsspp/issues/17425 "Android: Explicitly allow content URI intents"
[#17412]: https://github.com/hrydgard/ppsspp/issues/17412 "Controller mapping fixes"
[#17420]: https://github.com/hrydgard/ppsspp/issues/17420 "Fix glitch when mapping analog inputs, caused"
[#17489]: https://github.com/hrydgard/ppsspp/issues/17489 "Restore \"low end radius\" (inverse deadzone) for tilt input"
[#17473]: https://github.com/hrydgard/ppsspp/issues/17473 "Revert \"Remove the Android display resolution selector\""
[#17467]: https://github.com/hrydgard/ppsspp/issues/17467 "Make the DJ Max workaround more aggressive about hiding stuff."
[#17466]: https://github.com/hrydgard/ppsspp/issues/17466 "Fix running some file formats from the Downloads folder"
[#17440]: https://github.com/hrydgard/ppsspp/issues/17440 "Revert lmode variant reduction"
[#17439]: https://github.com/hrydgard/ppsspp/issues/17439 "Revert \"Merge pull request #16628 from hrydgard/remove-fog-fshader-flag\""
[#17475]: https://github.com/hrydgard/ppsspp/issues/17475 "Add a trivial profiling tool to the OpenGL backend"
[#17435]: https://github.com/hrydgard/ppsspp/issues/17435 "ElfLoader: Don't scan for functions in zero-length sections"
[#17442]: https://github.com/hrydgard/ppsspp/issues/17442 "Shrink the GLRRenderCommand struct from 152 to 88"
[#17457]: https://github.com/hrydgard/ppsspp/issues/17457 "UI: Fix thread error on zip open failure"
[#17486]: https://github.com/hrydgard/ppsspp/issues/17486 "Build fixes for OpenBSD"
[#17446]: https://github.com/hrydgard/ppsspp/issues/17446 "OpenGL: Combine some render commands"