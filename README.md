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

If you want to download regularly updated builds for Android, Windows x86 and x64, [visit Orphis' buildbot](https://buildbot.orphis.net/ppsspp/)

For game compatibility, see [community compatibility feedback](https://report.ppsspp.org/games).

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
[#16690]: https://github.com/hrydgard/ppsspp/issues/16690 "softgpu: Detect binner alloc fail and bail"
[#16710]: https://github.com/hrydgard/ppsspp/issues/16710 "GLES: Use uint for uint shift amounts"
[#16709]: https://github.com/hrydgard/ppsspp/issues/16709 "Correct some shader errors in reporting"
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
[#17398]: https://github.com/hrydgard/ppsspp/issues/17398 "OpenXR - Cleanup unsupported features, support Android 12"
[#17406]: https://github.com/hrydgard/ppsspp/issues/17406 "Even more crash fixes"
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
