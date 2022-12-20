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