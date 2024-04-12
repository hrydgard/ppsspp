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

What's new in 1.17.1
--------------------
- Fixed green rendering errors on some PowerVR GPUs ([#18781])
- Release all held keys on pause to avoid "stuck keys" after unpausing or in run-behind-pause ([#18786])
- UI fixes ([#18785])
- Update libchdr with zstd support, warn the user about bad CHDs ([#18824], [#18803])
- Add workaround for AdHoc mode in Resistance ([#18821])
- Fix graphics in Tokimeki Memorial 4 ([#18822])
- Fix crash in UFC 2010 on Mali GPUs ([#18813])
- Temporarily disable MSAA on Adreno GPUs due to crashing ([#18819])
- Fixed some crashes and optimized the game metadata cache ([#18794], [#18775])
- Additional crashfixes and similar ([#18777], [#18779], [#18823])
- Fixed playback of frame dumps with Vulkan ([#18793])
- Volume slider added for RetroAchievements sounds ([#18772])

What's new in 1.17
------------------
- Rendering performance and fixes
	- Fix for very old rendering issue in Tokimeki Memorial 4 ([#18650]) (didn't quite work, see 1.17.1)
	- Performance improvement in Tactics Ogre by avoiding a readback ([#18599])
	- Cull small draws that are quick to check that they are offscreen ([#18446])
	- Assorted optimizations ([#18586], [#18573], [#18476], [#18413], [#18416], [#18219])
	- Fix HUD in Tiger Woods 06 ([#18554])
	- Adrenotools support added - you can now load custom Vulkan drivers on Adreno ([#18532], [#18548], [#18699])
	- Fix rendering with bad Z parameters, fixes homebrew Zig example ([#18535])
	- Fix Z problem in Hayate no Gotoku!! Nightmare Paradise [#18478]
	- Fix frozen pitch meters in MLB games ([#18484])
	- Enable MSAA on modern mobile devices (Vulkan-only) ([#18472])
	- Fix video flicker in Naruto: Ultimate Ninja Heroes 2 ([#18454])
- UI
	- Track total time played per game ([#18442])
	- When opening the pause menu, there's now an option to keep the game running behind the menu.
	  This is enforced in multiplayer to avoid inadvertent desyncs ([#18517], [#18515])
- ISO loading improvements
	- The CHD file format is now fully supported (use `chdman createdvd`!), including with Remote ISO and Retroachievements
	- Improvements to [remote ISO](https://www.ppsspp.org/docs/reference/disc-streaming/): optional tab on home screen, can now share whole folders ([#18627], [#18639], [#18640], [#18631], [#18632], [#18633],)
- Controller and touchscreen fixes
	- More control bindings, organize into categories ([#18635], [#18589])
	- Fix inverse deadzone (low end) for joystick input
	- Fix analog deadzones for XInput controllers ([#18407])
	- Improved tilt control further ([#18533])
	- Mouse input supported on Android ([#18551], [#18473])
	- Customizable threshold for analog-trigger to button-press mapping, customizable delay for mouse-scroll key-up ([#18621], [#18585])
	- Make it work better to bind an analog stick to four buttons, useful for camera control in some games
	- Can now unpause with the key you bound to pause ([#18591])
- Other fixes and updates
	- More work on the IR JITs ([#18234], [#18235], [#18228], [#18227], [#18226], many more)
	- Moving the memstick directory on Android is now faster and safer [#18744]
	- Workaround problems in Add Doko Demo Issho, Driver 76, Harukanaru Toki no Naka, Ace Combat by slowing down the emulated UMD drive to match reality better ([#18436], [#18445])
	- VR: Quest 3 rendering issues fixed ([#18677])
	- Various bugfixes in texture replacement ([#18638], [#18610], [#18519], [#18466], [#18747])
	- RetroAchievements: Rich presence, renamed Challenge Mode back to the recommended Hardcore Mode, various error handling improvements, configure per game ([#18468], [#18651], [#18488], [#18428], [#18425])
	- HLE: Slice large-and-slow memcpy/memset operations, can help with some stalls. ([#18560])
	- Other various minor fixes and optimizations ([#18558], [#18555], [#18538], [#18529], [#18450], [#18314], [#18233], [#18678], [#18749], [#18736], [#18704])
	- SoftGPU fixes ([#18362])
	- Fixed international fonts on Steam Deck ([#18732], [#18734])
	- GoExplore (GPS app) now starts up and allows navigation ([#18665], [#18666], [#18668], [#18669])
	- SDL: Improve input latency in Vulkan mode by running rendering on a separate thread ([#18268])
	- Assorted multiplayer fixes ([#18435])
	- Support for emulating the infrared port of the original PSP through sceSircs ([#18684])

What's new in 1.16.6
--------------------
- Fix performance issue with Vulkan descriptor set allocation ([#18332])
- Smoother loading of replacement textures
- Fix the store on iOS ([#18323])
- Fix problem with waves background ([#18310])
- Some translation updates
- Other minor fixes

What's new in 1.16.5
--------------------
- Additional crash and memory-leak fixes ([#18243], [#18244], [#18247])
- Revert bad change that broke things with hardware transform turned off ([#18261])
- Fix menu texture problem in Naruto Kizuna Drive in OpenGL ([#18255])
- Apply flicker fix to WWE SvR 2007
- More efficient handling of accelerometer events ([#18250])
- Fix for jumpy graphics in Harvest Moon ([#18249])

What's new in 1.16.4
--------------------
- Simplify shader cache lookups on Vulkan, hopefully fixing some crashes ([#18218])
- Assorted improvements to the IR JITs ([#18228], [#18235], [#18211], more)
- Other crash and stability fixes ([#18221], [#18220], [#18230], [#18216])
- Some translation updates ([#18237], more)
- Cleanups and assert fixes ([#18205], [#18201], [#18206])

What's new in 1.16.3
--------------------
- Fix crash bug and performance issue in Vulkan shader cache ([#18183], [#18189])
- Fix crash in icon loading in homebrew store ([#18185])
- Add some memory safety check ([#18184], [#18194])
- Fix problem when changing backend from the Windows menu ([#18182])

What's new in 1.16.2
--------------------
- Fix for HTTP support on Linux on networks with shaky or incomplete IPv6 support
- Assorted fixes for leaks and crashes ([#18169], [#18151])
- Fix hang when switching UMD with RetroAchievements enabled ([#18143])
- Fix math bug in new IR JIT for x86 ([#18165])
- Minor math optimization -fno-math-errno ([#18158])
- Fix for software renderer crash

What's new in 1.16.1
--------------------

- Move RetroAchievements to the Tools tab in settings ([#18127])
- Fix graphics regressions in Hot Shots Golf 2 / Everybody's Golf 2 and Final Fantasy Tactics ([#18142])
- Fix hang on startup with OpenGL, that happened often if "buffer commands" was set to off.
- Fix problem with the sc instruction that broke Beats ([#18133], [#18140])
- Fix problem with the chat window accidentally closing on typing X ([#18135])
- Fix some crashes, add some asserts and reporting hooks ([#18129])
- Fix some text rendering in the software renderer ([#18126])

What's new in 1.16
------------------
Special thanks to unknownbrackets for the new JIT compilers, and fp64 for finally cracking the vrnd instruction.

- RetroAchievements support ([#17589], [#17631], many more). See [RetroAchievements on ppsspp.org](https://www.ppsspp.org/docs/reference/retro-achievements).
- New JIT backends:
  - RISC-V, based on IR
  - x86 JIT based on IR. Often faster than the existing one.
- Input fixes
  - Improve behavior when analog and digital inputs clash ([#17960])
  - Combo mapping is now disabled by default ([#17673])
  - Android: Better tracking of devices names ([#17562], auto config)
  - Fix mapping custom touch buttons to analog inputs ([#17433])
- Rendering performance and fixes
  - Fix flicker in WWE Smackdown vs Raw 2006 ([#18009]), video flicker in Naruto 2 ([#18008])
  - Fix bad colors in Syphon Filter: Logan's Shadow menu ([#17965])
  - On lower-end devices, avoid "uber" shaders due to performance and driver bugs ([#17449], [#17922])
  - Allow disabling V-sync on Android with Vulkan, more SDL platforms ([#17903], [#18087])
  - On Vulkan, reduce input lag when "Buffer graphics commands" is set to off ([#17831])
  - Assorted minor perf ([#17810], [#17505], [#17478], [#17471], [#17452], [#17446], [#17442])
  - Fix shadows in MotorStorm ([#17602]) (not actually a rendering problem)
  - Fix rendering issue in Lunar Silver Star ([#17451])
  - Add a cache for MakePixelsTexture, improving perf in God of War ([#17534])
  - Lots of software renderer improvements ([#17571], [#17569], [#17619], [#17621], [#17618], [#17609], ...)
- Networking
  - HTTPS support now enabled in store and for RA on Windows, Android, Mac, iOS ([#17744], ...)
  - Ad-hoc: Fix for Metal Gear Acid issue with Link Battle ([#17947])
- Texture replacement fixes
  - Fix Tactics Ogre texture issues ([#18001], [#18011])
  - Fix problem with anisotropic filtering ([#17930])
  - Fix glitches on D3D11 with KTX2 textures ([#17749])
- UI changes
  - Color emoji support on some platforms ([#17854], [#17856])
  - Use TTF fonts on SDL where available (macOS, Linux/Steam Deck) ([#17844]), support HiDPI ([#17651])
  - Allow setting the PSP's MAC address directly ([#17755])
  - Better looking notifications ([#17606], [#17674], [#17672])
- Cheats
  - Fix loading cheat dbs on Android devices with scoped storage ([#17834])
- VR (Quest, other Android VR devices)
  - Cinema screen mode improvements ([#17704], [#17752])
  - Quest-only passthrough mode ([#17591])
  - Cleanups, compatibility fixes, make VR settings per-game ([#17636], [#17656], [#17598], [#17518])
- Other
  - Fix horrible audio glitch in After Burner ([#18076])
  - Emulate the vrnd instruction accurately ([#17506], [#17549])
  - Fix timing issue causing slowdowns in MLB games ([#17676], [#17677])
  - UWP keyboard support, many other updates ([#17952], [#17974])
  - Allow choosing the display resolution on Android again ([#17473])
  - Fix issue running some file types out of the Download folder on Android ([#17466])

Older news
----------
Looking for [older news](history.md)?

You can also find the full update history on [the website](https://www.ppsspp.org/news).

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
[#17473]: https://github.com/hrydgard/ppsspp/issues/17473 "Revert \"Remove the Android display resolution selector\""
[#17466]: https://github.com/hrydgard/ppsspp/issues/17466 "Fix running some file formats from the Downloads folder"
[#17442]: https://github.com/hrydgard/ppsspp/issues/17442 "Shrink the GLRRenderCommand struct from 152 to 88"
[#17446]: https://github.com/hrydgard/ppsspp/issues/17446 "OpenGL: Combine some render commands"
[#17589]: https://github.com/hrydgard/ppsspp/issues/17589 "Retroachievements support (work-in-progress)"
[#17631]: https://github.com/hrydgard/ppsspp/issues/17631 "RetroAchievements: Remaining features to implement"
[#17960]: https://github.com/hrydgard/ppsspp/issues/17960 "Controls: Make the analog/digital mapping clash resolution more gentle."
[#17673]: https://github.com/hrydgard/ppsspp/issues/17673 "Add checkbox controlling if new mappings can be combo mappings."
[#17562]: https://github.com/hrydgard/ppsspp/issues/17562 "Android: Correct pad name tracking"
[#17433]: https://github.com/hrydgard/ppsspp/issues/17433 "Custom button analog fix"
[#18009]: https://github.com/hrydgard/ppsspp/issues/18009 "Enable [DisallowFramebufferAtOffset] for WWE SvR 2006."
[#18008]: https://github.com/hrydgard/ppsspp/issues/18008 "Add heuristic for memory->framebuffer copies, fixing video flicker in Naruto UNH 2"
[#17965]: https://github.com/hrydgard/ppsspp/issues/17965 "Add compat flag to not load CLUTs from old framebuffers"
[#17449]: https://github.com/hrydgard/ppsspp/issues/17449 "Fragment \"ubershader\" option"
[#17922]: https://github.com/hrydgard/ppsspp/issues/17922 "Add checkboxes in developer tools to allow disabling ubershaders"
[#17903]: https://github.com/hrydgard/ppsspp/issues/17903 "Present modes refactor"
[#18087]: https://github.com/hrydgard/ppsspp/issues/18087 "Vulkan: Fix disabling VSync on SDL platforms that support IMMEDIATE but not MAILBOX"
[#17831]: https://github.com/hrydgard/ppsspp/issues/17831 "Vulkan: Don't use multithreaded rendering if buffer commands (frames in flight) is set to 1"
[#17810]: https://github.com/hrydgard/ppsspp/issues/17810 "Cache computed planes used for BBOX culling"
[#17505]: https://github.com/hrydgard/ppsspp/issues/17505 "Only dirty the uniform UVSCALEOFFSET when really needed"
[#17478]: https://github.com/hrydgard/ppsspp/issues/17478 "OpenGL: Efficiency improvements for stencil commands"
[#17471]: https://github.com/hrydgard/ppsspp/issues/17471 "Reduce zero initialization and copying overhead of render commands"
[#17452]: https://github.com/hrydgard/ppsspp/issues/17452 "Don't cache render target copies for shader blending, only cache copies for overlap"
[#17602]: https://github.com/hrydgard/ppsspp/issues/17602 "Loader: Support HI16/16 pairs, not just LO16"
[#17451]: https://github.com/hrydgard/ppsspp/issues/17451 "Rendering issue with tornado in Lunar Silver Star"
[#17534]: https://github.com/hrydgard/ppsspp/issues/17534 "Add a cache for MakePixelsTexture"
[#17571]: https://github.com/hrydgard/ppsspp/issues/17571 "softgpu: Use SIMD more for dot products"
[#17569]: https://github.com/hrydgard/ppsspp/issues/17569 "ARM64: Optimize saved registers in vertex decoder."
[#17619]: https://github.com/hrydgard/ppsspp/issues/17619 "softgpu: Improve Z interpolation SIMD"
[#17621]: https://github.com/hrydgard/ppsspp/issues/17621 "softgpu: Ensure early depth test uses SIMD"
[#17618]: https://github.com/hrydgard/ppsspp/issues/17618 "Optimize casts in softgpu"
[#17609]: https://github.com/hrydgard/ppsspp/issues/17609 "softgpu: Optimize (bi-)linear texture filtering"
[#17744]: https://github.com/hrydgard/ppsspp/issues/17744 "Initial HTTPS support via Naett (partial platform support)"
[#17947]: https://github.com/hrydgard/ppsspp/issues/17947 "[Adhocctl] Fix for Metal Gear Acid issue"
[#18001]: https://github.com/hrydgard/ppsspp/issues/18001 "Enable the FakeMipmapChange flag for US/EU Tactics Ogre, fixing replacement problem."
[#18011]: https://github.com/hrydgard/ppsspp/issues/18011 "Detect the simplest Tactics Ogre case (US/EU) early"
[#17930]: https://github.com/hrydgard/ppsspp/issues/17930 "Enable anisotropic filtering for replacement textures with mipmaps"
[#17749]: https://github.com/hrydgard/ppsspp/issues/17749 "In D3D11, force block compressed textures to have dimensions divisible"
[#17854]: https://github.com/hrydgard/ppsspp/issues/17854 "Implement color emoji support for Android"
[#17856]: https://github.com/hrydgard/ppsspp/issues/17856 "Windows UWP: Enable color emoji rendering through DirectWrite"
[#17844]: https://github.com/hrydgard/ppsspp/issues/17844 "SDL: text renderer fixes, and CI"
[#17651]: https://github.com/hrydgard/ppsspp/issues/17651 "SDL: support HiDPI on wayland"
[#17755]: https://github.com/hrydgard/ppsspp/issues/17755 "Allow entering an exact Mac address, while keeping the randomization ability"
[#17606]: https://github.com/hrydgard/ppsspp/issues/17606 "OSD: Add colored backgrounds to OSD messages, according to type."
[#17674]: https://github.com/hrydgard/ppsspp/issues/17674 "New UI view: Notice"
[#17672]: https://github.com/hrydgard/ppsspp/issues/17672 "Android: Show some Java exceptions as they happen"
[#17834]: https://github.com/hrydgard/ppsspp/issues/17834 "OpenCFile: Fix Android content-uri append mode"
[#17704]: https://github.com/hrydgard/ppsspp/issues/17704 "OpenXR - Enhancements of cinema-style screen"
[#17752]: https://github.com/hrydgard/ppsspp/issues/17752 "OpenXR - Enable 6DoF in cinema mode"
[#17591]: https://github.com/hrydgard/ppsspp/issues/17591 "OpenXR - Add passthrough option (Quest only)"
[#17636]: https://github.com/hrydgard/ppsspp/issues/17636 "OpenXR - Major review"
[#17656]: https://github.com/hrydgard/ppsspp/issues/17656 "OpenXR - Game compatibility fixes"
[#17598]: https://github.com/hrydgard/ppsspp/issues/17598 "OpenXR - Disable stereo for Ultimate Ghosts and Goblins"
[#17518]: https://github.com/hrydgard/ppsspp/issues/17518 "OpenXR - Enable VR settings per game"
[#18076]: https://github.com/hrydgard/ppsspp/issues/18076 "SasAudio: Always reinitialize the VAG decoder on sceSasSetVoice, even if already playing"
[#17506]: https://github.com/hrydgard/ppsspp/issues/17506 "Emulating HW vrnd"
[#17549]: https://github.com/hrydgard/ppsspp/issues/17549 "Fix vrnd to the current understanding"
[#17676]: https://github.com/hrydgard/ppsspp/issues/17676 "Reduce delays in sceKernelReferThreadProfiler/ReferGlobalProfiler."
[#17677]: https://github.com/hrydgard/ppsspp/issues/17677 "Kernel: Use lower profiler func timing"
[#17952]: https://github.com/hrydgard/ppsspp/issues/17952 "[UWP] Improvements 2 (Configs, Render, Input)"
[#17974]: https://github.com/hrydgard/ppsspp/issues/17974 "(UWP) Another Round of Code Cleanups"
[#18127]: https://github.com/hrydgard/ppsspp/issues/18127 "Move RetroAchievements to the tools tab, fix leaderboard submitted notification positioning"
[#18142]: https://github.com/hrydgard/ppsspp/issues/18142 "Revert \"Merge pull request #18008 from hrydgard/naruto-video-flicker-heuristic\""
[#18143]: https://github.com/hrydgard/ppsspp/issues/18143 "Fix UMD disc swap with Retroachievements enabled"
[#18133]: https://github.com/hrydgard/ppsspp/issues/18133 "More sensible approach to the sc problem that broke Beats"
[#18140]: https://github.com/hrydgard/ppsspp/issues/18140 "x86jit: Fix spill on sc in longer block"
[#18135]: https://github.com/hrydgard/ppsspp/issues/18135 "Fix closing the chat window with ESC, add some asserts"
[#18129]: https://github.com/hrydgard/ppsspp/issues/18129 "Fix the semantics of DenseHashMap to be consistent even when inserting nulls"
[#18126]: https://github.com/hrydgard/ppsspp/issues/18126 "PPGe: Use texture windows for atlas text"
[#18169]: https://github.com/hrydgard/ppsspp/issues/18169 "Better handling of shadergen failures, other minor things"
[#18151]: https://github.com/hrydgard/ppsspp/issues/18151 "GPU, VFS, UI: Fixed minor memleaks"
[#18165]: https://github.com/hrydgard/ppsspp/issues/18165 "x86jit: Fix flush for special-purpose reg"
[#18158]: https://github.com/hrydgard/ppsspp/issues/18158 "Add -fno-math-errno"
[#18183]: https://github.com/hrydgard/ppsspp/issues/18183 "Pipeline/shader race-condition-during-shutdown crash fix"
[#18189]: https://github.com/hrydgard/ppsspp/issues/18189 "Be a bit smarter when loading the shader cache, avoid duplicating work"
[#18185]: https://github.com/hrydgard/ppsspp/issues/18185 "Store: Fix race condition causing crashes if looking at another game before an icon finishes downloading"
[#18184]: https://github.com/hrydgard/ppsspp/issues/18184 "Add memory bounds-check when expanding points, rects and lines to triangles"
[#18194]: https://github.com/hrydgard/ppsspp/issues/18194 "Cleanups and comment clarifications"
[#18182]: https://github.com/hrydgard/ppsspp/issues/18182 "Backend change from Win32 menu: Add quick workaround for instance counter misbehavior"
[#18218]: https://github.com/hrydgard/ppsspp/issues/18218 "Vulkan: Simplify GetShaders and DirtyLastShader, making them internally consistent."
[#18228]: https://github.com/hrydgard/ppsspp/issues/18228 "unittest: Add jit compare for jit IR"
[#18235]: https://github.com/hrydgard/ppsspp/issues/18235 "irjit: Handle VDet"
[#18211]: https://github.com/hrydgard/ppsspp/issues/18211 "More crash fix attempts"
[#18221]: https://github.com/hrydgard/ppsspp/issues/18221 "Some cleanups and fixes to obscure crashes"
[#18220]: https://github.com/hrydgard/ppsspp/issues/18220 "Add some missing locking in KeyMap.cpp."
[#18230]: https://github.com/hrydgard/ppsspp/issues/18230 "Android: Minor activity lifecycle stuff"
[#18216]: https://github.com/hrydgard/ppsspp/issues/18216 "Don't load the shader cache on a separate thread - all it does is already async"
[#18237]: https://github.com/hrydgard/ppsspp/issues/18237 "UI/localization: Italian translation update"
[#18205]: https://github.com/hrydgard/ppsspp/issues/18205 "http: Fix errors on connect"
[#18201]: https://github.com/hrydgard/ppsspp/issues/18201 "Asserts and checks"
[#18206]: https://github.com/hrydgard/ppsspp/issues/18206 "GPU: Handle invalid blendeq more accurately"
[#18243]: https://github.com/hrydgard/ppsspp/issues/18243 "More crashfix/leakfix attempts"
[#18244]: https://github.com/hrydgard/ppsspp/issues/18244 "Core: Stop leaking file loaders"
[#18247]: https://github.com/hrydgard/ppsspp/issues/18247 "Jit: Assert on bad exit numbers, allow two more exits per block"
[#18261]: https://github.com/hrydgard/ppsspp/issues/18261 "Revert \"Merge pull request #18184 from hrydgard/expand-lines-mem-fix\""
[#18255]: https://github.com/hrydgard/ppsspp/issues/18255 "Fix issue uploading narrow textures in OpenGL."
[#18250]: https://github.com/hrydgard/ppsspp/issues/18250 "Separate out accelerometer events from joystick axis events"
[#18249]: https://github.com/hrydgard/ppsspp/issues/18249 "arm64jit: Avoid fused multiplies in vcrsp.t"
[#18332]: https://github.com/hrydgard/ppsspp/issues/18332 "We somehow lost the usage_ counter increment in VulkanDescSetPool, fix that"
[#18323]: https://github.com/hrydgard/ppsspp/issues/18323 "Turn off HTTPS support for iOS."
[#18310]: https://github.com/hrydgard/ppsspp/issues/18310 "Fix waves background"
[#18650]: https://github.com/hrydgard/ppsspp/issues/18650 "Allow merging framebuffers vertically like the old Juiced 2 fix."
[#18599]: https://github.com/hrydgard/ppsspp/issues/18599 "Tactics Ogre: Remove a redundant GPU readback operation"
[#18446]: https://github.com/hrydgard/ppsspp/issues/18446 "Minor bbox optimizations, assorted bugfixes"
[#18586]: https://github.com/hrydgard/ppsspp/issues/18586 "DrawEngine: Avoid decoding indices when we don't need them."
[#18573]: https://github.com/hrydgard/ppsspp/issues/18573 "Defer frame waits if possible"
[#18476]: https://github.com/hrydgard/ppsspp/issues/18476 "VertexDecoder: Minor optimization for x86/64 CPUs not supporting SSE4."
[#18413]: https://github.com/hrydgard/ppsspp/issues/18413 "Cache and hash data for DrawPixels"
[#18416]: https://github.com/hrydgard/ppsspp/issues/18416 "Optimize DrawPixels for 16-bit RGB565 copies"
[#18219]: https://github.com/hrydgard/ppsspp/issues/18219 "Make GetIndexBounds friendlier to autovectorization. Works on x86 at least."
[#18554]: https://github.com/hrydgard/ppsspp/issues/18554 "DXT5: Fix decoding of alpha channel for textures with a non-mod-4 width."
[#18532]: https://github.com/hrydgard/ppsspp/issues/18532 "Android: Implement custom driver loading for ARM64 Android devices"
[#18548]: https://github.com/hrydgard/ppsspp/issues/18548 "Adrenotools followup"
[#18699]: https://github.com/hrydgard/ppsspp/issues/18699 "Adrenotools driver installation: Fix bad error checks"
[#18535]: https://github.com/hrydgard/ppsspp/issues/18535 "Eliminate inf values resulting from depth range computation."
[#18478]: https://github.com/hrydgard/ppsspp/issues/18478 "Handle block transfers from RAM to depth buffers."
[#18484]: https://github.com/hrydgard/ppsspp/issues/18484 "Fix frozen pitch meters in MLB series games - we were not hashing enough texture data"
[#18472]: https://github.com/hrydgard/ppsspp/issues/18472 "Vulkan: Allow MSAA on modern-ish mobile devices, but add a little warning sign."
[#18454]: https://github.com/hrydgard/ppsspp/issues/18454 "Naruto Ultimate Ninja Heroes 2 video flicker fix: Take 3"
[#18442]: https://github.com/hrydgard/ppsspp/issues/18442 "Track time-played per game"
[#18517]: https://github.com/hrydgard/ppsspp/issues/18517 "Run-behind-pause: Fix some edge cases with the transparent background setting"
[#18515]: https://github.com/hrydgard/ppsspp/issues/18515 "Run behind pause screen"
[#18627]: https://github.com/hrydgard/ppsspp/issues/18627 "Remote game streaming: Add an option to put a tab on the main screen"
[#18639]: https://github.com/hrydgard/ppsspp/issues/18639 "Various changes to the webserver to handle serving HTTP subfolders"
[#18640]: https://github.com/hrydgard/ppsspp/issues/18640 "Various fixes to PathBrowser etc to handle browsing HTTP subfolders"
[#18631]: https://github.com/hrydgard/ppsspp/issues/18631 "Remote ISO: Prepare to allow sharing folders directly"
[#18632]: https://github.com/hrydgard/ppsspp/issues/18632 "Remote ISO: Allow sharing a full folder instead of Recent"
[#18633]: https://github.com/hrydgard/ppsspp/issues/18633 "Remote ISO: Add working support for streaming CHD files over the network"
[#18635]: https://github.com/hrydgard/ppsspp/issues/18635 "Add bindings for toggling mouse control and touch screen controls"
[#18589]: https://github.com/hrydgard/ppsspp/issues/18589 "Control Mappings: Organize bindings into categories"
[#18407]: https://github.com/hrydgard/ppsspp/issues/18407 "skip xinput trigger threshold check"
[#18533]: https://github.com/hrydgard/ppsspp/issues/18533 "Tilt: Bugfix, make the deadzone circular, in addition to the inverse (low end radius)."
[#18551]: https://github.com/hrydgard/ppsspp/issues/18551 "Mouse refactor, restore smoothing"
[#18473]: https://github.com/hrydgard/ppsspp/issues/18473 "Add mouse wheel scrolling support for Android to the UI"
[#18621]: https://github.com/hrydgard/ppsspp/issues/18621 "Add \"Analog trigger threshold\" setting, for conversion of analog trigger inputs to digital button inputs."
[#18585]: https://github.com/hrydgard/ppsspp/issues/18585 "Add setting to configure simulated key-up delay for mouse wheel events"
[#18591]: https://github.com/hrydgard/ppsspp/issues/18591 "Allow unpausing with keys bound to pause"
[#18234]: https://github.com/hrydgard/ppsspp/issues/18234 "x86jit: Perform vector transfers instead of flushing to memory"
[#18227]: https://github.com/hrydgard/ppsspp/issues/18227 "x86jit: Flush floats together if possible"
[#18226]: https://github.com/hrydgard/ppsspp/issues/18226 "x86jit: Improve memory breakpoint speed"
[#18744]: https://github.com/hrydgard/ppsspp/issues/18744 "Memstick folder move on Android: Speedup and safety"
[#18436]: https://github.com/hrydgard/ppsspp/issues/18436 "Add Doko Demo Issho ,Driver 76, 	Harukanaru Toki no Naka de 3 with Izayoiki Aizouban into ForceUMDReadSpeed"
[#18445]: https://github.com/hrydgard/ppsspp/issues/18445 "Make 3 games into compat"
[#18677]: https://github.com/hrydgard/ppsspp/issues/18677 "Update README.md for 1.17"
[#18638]: https://github.com/hrydgard/ppsspp/issues/18638 "Fix two minor tex replacement issues"
[#18610]: https://github.com/hrydgard/ppsspp/issues/18610 "Texture replacer: Fix for texture directories missing an ini file"
[#18519]: https://github.com/hrydgard/ppsspp/issues/18519 "Enable texture replacement filtering overrides even if file is missing"
[#18466]: https://github.com/hrydgard/ppsspp/issues/18466 "Texture replacement: Prioritize ini file [hashes] section over just files in the \"root\" folder."
[#18747]: https://github.com/hrydgard/ppsspp/issues/18747 "Texture saving fixes, icon load fix"
[#18468]: https://github.com/hrydgard/ppsspp/issues/18468 "RetroAchievements: Show rich presence message on pause screen, restriction tweaks"
[#18651]: https://github.com/hrydgard/ppsspp/issues/18651 "HTTPS through naett: Get the body of the response even if code isn't 200"
[#18488]: https://github.com/hrydgard/ppsspp/issues/18488 "Make some achievement settings (including Hardcore mode) configurable per-game."
[#18428]: https://github.com/hrydgard/ppsspp/issues/18428 "Forgot some cases where I need to enable save (but not load) state in challenge mode, if the option is set"
[#18425]: https://github.com/hrydgard/ppsspp/issues/18425 "RetroAchievements: Add option to allow saving, but not loading, in challenge / hardcore mode."
[#18560]: https://github.com/hrydgard/ppsspp/issues/18560 "HLE: Slice the very slow memset/memcpy variants"
[#18558]: https://github.com/hrydgard/ppsspp/issues/18558 "Enforce a max size for save state screenshot regardless of resolution mode"
[#18555]: https://github.com/hrydgard/ppsspp/issues/18555 "Vulkan: UI texture loading error handling fixes"
[#18538]: https://github.com/hrydgard/ppsspp/issues/18538 "Async texture load on Pause screen"
[#18529]: https://github.com/hrydgard/ppsspp/issues/18529 "Android: Add option to ask system for 60hz output"
[#18450]: https://github.com/hrydgard/ppsspp/issues/18450 "Enable some NEON optimizations on ARM32 that we only had on ARM64 before"
[#18314]: https://github.com/hrydgard/ppsspp/issues/18314 "Interpreter: Optimize ReadVector/WriteVector"
[#18233]: https://github.com/hrydgard/ppsspp/issues/18233 "Use a thread for meminfo and defer tag lookup for copies"
[#18678]: https://github.com/hrydgard/ppsspp/issues/18678 "Vulkan: Fix trying to compare uninitialized parts of packed descriptors"
[#18749]: https://github.com/hrydgard/ppsspp/issues/18749 "HTTPClient: Fix socket leak on connect failure"
[#18736]: https://github.com/hrydgard/ppsspp/issues/18736 "CwCheats: Retry looking in g_gameInfoCache until the data is there."
[#18704]: https://github.com/hrydgard/ppsspp/issues/18704 "Revert back to the old way of fitting into 16:9: Crop one line at the top and bottom"
[#18362]: https://github.com/hrydgard/ppsspp/issues/18362 "softgpu: Point depthbuf at the first VRAM mirror"
[#18732]: https://github.com/hrydgard/ppsspp/issues/18732 "SDL fonts: Add \"Droid Sans Fallback\" to the list of fallback fonts."
[#18734]: https://github.com/hrydgard/ppsspp/issues/18734 "SDL fallback fonts: Add more font names"
[#18665]: https://github.com/hrydgard/ppsspp/issues/18665 "Fix Go!Explore🗺️🧭 issue with GetDirListing(/); closes #15932"
[#18666]: https://github.com/hrydgard/ppsspp/issues/18666 "GPS: Improve emulation to enable Go!Explore navigation"
[#18668]: https://github.com/hrydgard/ppsspp/issues/18668 "GPS: Set valid values and request updates on savestate loading"
[#18669]: https://github.com/hrydgard/ppsspp/issues/18669 "GPS: updates"
[#18268]: https://github.com/hrydgard/ppsspp/issues/18268 "SDL: Use an \"EmuThread\" in Vulkan mode"
[#18435]: https://github.com/hrydgard/ppsspp/issues/18435 "An attempt to fix Tekken 6 stuck issue when exiting Lob"
[#18684]: https://github.com/hrydgard/ppsspp/issues/18684 "sceSircs/Infrared support on Android"
[#18781]: https://github.com/hrydgard/ppsspp/issues/18781 "Disable 16-bit textures on PowerVR with Vulkan"
[#18794]: https://github.com/hrydgard/ppsspp/issues/18794 "More gameinfocache fixes"
[#18775]: https://github.com/hrydgard/ppsspp/issues/18775 "GameInfoCache: Keep properly track of what's already loaded, lots of cleanup"
[#18793]: https://github.com/hrydgard/ppsspp/issues/18793 "Fix GE framedump playback on Vulkan"
[#18786]: https://github.com/hrydgard/ppsspp/issues/18786 "Release all keys on pause."
[#18785]: https://github.com/hrydgard/ppsspp/issues/18785 "Fix issue with the collapsible sections in control mapping collapsing on every change, plus, combo fix"
[#18777]: https://github.com/hrydgard/ppsspp/issues/18777 "Expand primitives: Check the vertex count too."
[#18779]: https://github.com/hrydgard/ppsspp/issues/18779 "More fixes"
[#18772]: https://github.com/hrydgard/ppsspp/issues/18772 "Add volume slider for RetroAchievements sound effects"
[#18824]: https://github.com/hrydgard/ppsspp/issues/18824 "Update libchdr to the latest, which supports zstd blocks"
[#18803]: https://github.com/hrydgard/ppsspp/issues/18803 "Warn the user about bad CHDs"
[#18821]: https://github.com/hrydgard/ppsspp/issues/18821 "Hacky compat workaround for Resistance's ad-hoc mode"
[#18822]: https://github.com/hrydgard/ppsspp/issues/18822 "Avoid vertically merging the two main framebuffers, even if FramebufferAllowLargeVerticalOffset is on."
[#18813]: https://github.com/hrydgard/ppsspp/issues/18813 "Mali: Turn off any depth writes in the shader if depth test == NEVER"
[#18819]: https://github.com/hrydgard/ppsspp/issues/18819 "Temporarily disable MSAA on Adreno GPUs"
[#18823]: https://github.com/hrydgard/ppsspp/issues/18823 "Memory exception handler: Don't disassemble if ignoring the exception"
