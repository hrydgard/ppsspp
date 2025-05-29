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
	- Multiple rendering-related crash fixes ([#20270], [#20346], [#20344], ...)
	- Texture replacement load speed corrected and can now be controlled ([#20286]), regression fix for zipped packs ([#19860])
	- Additional assorted compatibility and other fixes ([#20376], [#20070], [#19685])
	- Persona 1/2 readback fixes ([#20067], [#20068])
	- Other correctness fixes ([#20233], [#20255], [#19686])

- General emulation
	- Allow disabling HLE on a per-module basis (if games ship the module so we can load it). Enabled for sceCcc ([#20218]), and more importantly scePsmf ([#20208]), fixing multiple issues.
	- Additional module loading improvements ([#20114], ...)
	- More precise sleep function on Windows ([#20054])
	- More replacement texture improvements, allow replacing game icons ([#19975], [#19978])
	- Disable certain features like fast-forward when networking is on, to avoid de-syncs ([])
	- Fixes mono Atrac3 as video soundtrack ([#19800])
	- Improvements to RISC-V support ([#20352, ...])

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
	- Developer Settings are now tabbed for easier access ([#20033], [#20228])
	- Switch to the full libpng API so we can disable gamma correction, like the real PSP ([])
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

What's new in 1.18.1
--------------------
- Crashfix in PBP reader ([#19580])
- Fix minor theme issue in the Homebrew Store ([#19582])

What's new in 1.18
------------------
- Platform support
	- Improved iOS/Mac support, Game Mode support ([#19287], [#19281], [#19269], [#19270], [#19257], [#19254], [#19244], [#19227], [#19224], [#19213], [#19200], [#19187], [#19184], [#19161], [#19118])
	- IR Interpreter: work on correctness and optimizations ([#19299], [#19280], [#19268], [#19265], [#19262], [#19260], [#19255], [#19252], [#19240], [#19233], [#19231], [#19193], [#19189], [#19173], [#19128])
	- Interpreted Vertex Decoder optimizations:  ([#19243], [#19241])
	- SDL fixes: ([#19131])
	- Legacy Edition for old Android TV (only for download on the website): ([#19401])
	- Android and Windows shortcuts - add icons and bugfixes: ([#19139], [#19142], [#19138])
	- Improve performance of CHD loading ([#18931])
- UI
	- Many crash, hang and performance fixes around the UI ([#18950], [#19561], [#19556], [#19531], [#19507], [#19523], [#19529], [#19482], [#19473], [#19438] [#19165])
	- New post proc filter "Sharp bilinear" ([#19530])
	- Misc UI fixes ([#19505], [#19492], [#19126], [#19020])
	- 3 new themes ([#19504])
	- Game info in-game ([#19471])
	- New remote iso UI ([#19293])
	- Install save games from ZIP file ([#19443], [#19436])
	- More input-mappable actions like Reset, allow mapping Alt key ([#19472], [#19484], [#19304])
	- Homebrew Store: Added website links, license info: ([#19425], [#19426])
	- RetroAchievements: Can now hash homebrew apps ([#19096]), RAIntegration support ([#19002])
	- Fix regression of the AxisSwap feature ([#19059])
- Emulation
	- Misc crash fixes: ([#19563], [#19546], [#19491], [#19358], [#19347], [#19198])
	- Vulkan barrier fixes: ( [#19009], [#19017], [#19018])
	- Misc compatibility fixes ([#19560])
	- Misc filesystem fixes (FDs, date/time) ([#19459], [#19340])
	- Workaround for The Warriors video playback ([#19450])
	- Expose GPI switches and GPO leds, accessible in developer settings
	- Fix regression caused by the fix for UFC Undisputed ([#18806])
	- Broke out the Atrac3+ code from ffmpeg for easier debugging and maintenance ([#19033], [#19053], more)
	- implement sysclib_sprintf ([#19097])
- Rendering fixes
	- Socom FB3 depth buffer problem in menu ([#19490])
	- Platypus: Glitchy transparency with OpenGL ([#19364])
	- Syphon Filter: Logan's Shadow: Dark lighting in OpenGL ([#19489])
	- MGS2 Acid errors on AMD GPUs ([#19439])
	- Fix regression in Genshou Suikoden ([#19122])
	- Fix HUD glitch in GTA LCS by emulating "provoking vertex" correctly ([#19334])
- Debugging improvements
	- MIPSTracer - a new (rough) debugging tool by Nemoumbra ([#19457])
- VR
	- New immersive mode (makes better use of reprojection by extending the rendered area) ([#19361])
	- A lot of fixes by Lubos ([#19420], [#19416], [#19389], [#19390], [#19361], [#19092], ...)
- Misc
	- The CHD file format is better supported and performance has been drastically improved ([#18924], [#18931])

[comment]: # (LINK_LIST_BEGIN_HERE)
[#19287]: https://github.com/hrydgard/ppsspp/issues/19287 "iOS: Enable \"double-swipe\" to switch apps"
[#19281]: https://github.com/hrydgard/ppsspp/issues/19281 "iOS: Disable the swipe-back gesture in-game, to maximize touch responsiveness"
[#19269]: https://github.com/hrydgard/ppsspp/issues/19269 "Set the games category in plists for Mac and iOS."
[#19270]: https://github.com/hrydgard/ppsspp/issues/19270 "Set GCSupportsGameMode in info.plist files for iOS and Mac"
[#19257]: https://github.com/hrydgard/ppsspp/issues/19257 "iOS: Implement basic physical keyboard support"
[#19254]: https://github.com/hrydgard/ppsspp/issues/19254 "iOS: Fix \"Home\" button on controllers (like the PS logo button on a PS4 controller)"
[#19244]: https://github.com/hrydgard/ppsspp/issues/19244 "JIT-less vertex decoder: SSE/NEON-optimize ComputeSkinMatrix"
[#19227]: https://github.com/hrydgard/ppsspp/issues/19227 "More text fixes on iOS/Mac"
[#19224]: https://github.com/hrydgard/ppsspp/issues/19224 "More iOS fixes"
[#19213]: https://github.com/hrydgard/ppsspp/issues/19213 "iOS: Prevent the Recents list from disappearing a lot"
[#19200]: https://github.com/hrydgard/ppsspp/issues/19200 "iOS: Add audio session mode controls"
[#19187]: https://github.com/hrydgard/ppsspp/issues/19187 "iOS: Fix issue with keyboard popping up after file picker."
[#19184]: https://github.com/hrydgard/ppsspp/issues/19184 "Native text drawing on macOS/iOS"
[#19161]: https://github.com/hrydgard/ppsspp/issues/19161 "Add basic soft-keyboard support on iOS"
[#19118]: https://github.com/hrydgard/ppsspp/issues/19118 "macOS: Update VulkanLoader for MoltenVK 1.2.8-style framework finding"
[#19299]: https://github.com/hrydgard/ppsspp/issues/19299 "IR Interpreter: Two small optimizations"
[#19280]: https://github.com/hrydgard/ppsspp/issues/19280 "Implement FPU rounding mode support in the IR interpreter"
[#19268]: https://github.com/hrydgard/ppsspp/issues/19268 "IRJit: If we're in \"JIT using IR\" mode, don't accidentally optimize for the interpreter."
[#19265]: https://github.com/hrydgard/ppsspp/issues/19265 "More minor IR optimizations"
[#19262]: https://github.com/hrydgard/ppsspp/issues/19262 "IR: Add some interpreter-only IR instructions for faster interpretation"
[#19260]: https://github.com/hrydgard/ppsspp/issues/19260 "More IR interpreter profiler work"
[#19255]: https://github.com/hrydgard/ppsspp/issues/19255 "Add built-in IR Interpreter profiler"
[#19252]: https://github.com/hrydgard/ppsspp/issues/19252 "Preparations for adding a performance profiler for the IR Interpreter"
[#19240]: https://github.com/hrydgard/ppsspp/issues/19240 "Store IR instructions in a bump-allocated vector instead of loose allocations"
[#19233]: https://github.com/hrydgard/ppsspp/issues/19233 "Minor IR Interpreter optimizations, other bugfixes"
[#19231]: https://github.com/hrydgard/ppsspp/issues/19231 "IR Interpreter: Some minor optimizations"
[#19193]: https://github.com/hrydgard/ppsspp/issues/19193 "IRInterpreter: Enable some optimizations that accidentally were only enabled on non-ARM64."
[#19189]: https://github.com/hrydgard/ppsspp/issues/19189 "IRInterpreter: Fix issue where we could accidentally optimize out CallReplacement ops."
[#19173]: https://github.com/hrydgard/ppsspp/issues/19173 "IRInterpreter compiler: Reject all vec2ops where the prefix is unknown while compiling"
[#19128]: https://github.com/hrydgard/ppsspp/issues/19128 "More IR interpreter optimizations"
[#19243]: https://github.com/hrydgard/ppsspp/issues/19243 "iOS: Implement accelerometer support"
[#19241]: https://github.com/hrydgard/ppsspp/issues/19241 "Optimize color conversions in non-JIT vertex decoder"
[#19131]: https://github.com/hrydgard/ppsspp/issues/19131 "CPU at 100% in menu in Vulkan on Linux"
[#19401]: https://github.com/hrydgard/ppsspp/issues/19401 "Android: Add new build config \"legacyOptimized\", which targets an older Android SDK version"
[#19139]: https://github.com/hrydgard/ppsspp/issues/19139 "Android: Upgrade SDK and target versions, implement shortcut icons"
[#19142]: https://github.com/hrydgard/ppsspp/issues/19142 "Android: Fix issue where shortcuts wouldn't override the currently running game."
[#19138]: https://github.com/hrydgard/ppsspp/issues/19138 "Windows: When using \"Create shortcut\", use the game's icon instead of PPSSPP's"
[#18931]: https://github.com/hrydgard/ppsspp/issues/18931 "CHD: Fix unnecessary reloads of \"hunks\" during large reads"
[#18950]: https://github.com/hrydgard/ppsspp/issues/18950 "Fix soft-lock when loading non-existing files, fix wrong timer in MIPSDebugInterface"
[#19561]: https://github.com/hrydgard/ppsspp/issues/19561 "Simplify reporting code (removing two threads), other minor fixes"
[#19556]: https://github.com/hrydgard/ppsspp/issues/19556 "Another bunch of pre-release fixes"
[#19531]: https://github.com/hrydgard/ppsspp/issues/19531 "Improve performance of UI text rendering"
[#19507]: https://github.com/hrydgard/ppsspp/issues/19507 "Prevent soft-locking the emulator on bad PBP files"
[#19523]: https://github.com/hrydgard/ppsspp/issues/19523 "Even more fixes"
[#19529]: https://github.com/hrydgard/ppsspp/issues/19529 "More misc minor fixes"
[#19482]: https://github.com/hrydgard/ppsspp/issues/19482 "Remove double ampersands from PPGe-drawn text (in-game UI)"
[#19473]: https://github.com/hrydgard/ppsspp/issues/19473 "Try to make Frame Advance a bit more reliable"
[#19438]: https://github.com/hrydgard/ppsspp/issues/19438 "Android memstick folder move: Minor logging and robustness improvements"
[#19165]: https://github.com/hrydgard/ppsspp/issues/19165 "UI crash fix in control mapping screen"
[#19530]: https://github.com/hrydgard/ppsspp/issues/19530 "Even more misc fixes: Beaterator, sharp bilinear, remove back button"
[#19505]: https://github.com/hrydgard/ppsspp/issues/19505 "iOS: Chat input fix, Mac text input fix"
[#19492]: https://github.com/hrydgard/ppsspp/issues/19492 "RetroAchievements login: Implement password masking"
[#19126]: https://github.com/hrydgard/ppsspp/issues/19126 "Allow taking screenshots in the app menu"
[#19020]: https://github.com/hrydgard/ppsspp/issues/19020 "Clickable notifications"
[#19504]: https://github.com/hrydgard/ppsspp/issues/19504 "Add 3 new themes"
[#19471]: https://github.com/hrydgard/ppsspp/issues/19471 "Add button to show the game-info screen from the in-game pause screen"
[#19293]: https://github.com/hrydgard/ppsspp/issues/19293 "Rework remote ISO UI a bit"
[#19443]: https://github.com/hrydgard/ppsspp/issues/19443 "More zip file install fixes"
[#19436]: https://github.com/hrydgard/ppsspp/issues/19436 "Implement save data install from ZIP"
[#19472]: https://github.com/hrydgard/ppsspp/issues/19472 "Add Reset as a mappable control"
[#19484]: https://github.com/hrydgard/ppsspp/issues/19484 "Add mappable devkit-only L2/L3/R2/R3 controls"
[#19304]: https://github.com/hrydgard/ppsspp/issues/19304 "Allow \"Alt\" to act like a normal keyboard input, if it's been mapped to something"
[#19425]: https://github.com/hrydgard/ppsspp/issues/19425 "Homebrew Store: Minor update adding license and website links"
[#19426]: https://github.com/hrydgard/ppsspp/issues/19426 "Additional store UI update"
[#19096]: https://github.com/hrydgard/ppsspp/issues/19096 "RetroAchievements: Add support for hashing homebrew"
[#19002]: https://github.com/hrydgard/ppsspp/issues/19002 "Add initial RAIntegration support through rc_client"
[#19059]: https://github.com/hrydgard/ppsspp/issues/19059 "Fix the AxisSwap feature - had a double mutex lock, oops."
[#19563]: https://github.com/hrydgard/ppsspp/issues/19563 "Vulkan: Fix potential crash from binding old CLUT textures"
[#19546]: https://github.com/hrydgard/ppsspp/issues/19546 "More assorted fixes"
[#19491]: https://github.com/hrydgard/ppsspp/issues/19491 "DrawEngineCommon: Enforce the limit on vertex decoding"
[#19358]: https://github.com/hrydgard/ppsspp/issues/19358 "Two crashfixes: Achievements menu, Outrun"
[#19347]: https://github.com/hrydgard/ppsspp/issues/19347 "sceFont and savestate fixes"
[#19198]: https://github.com/hrydgard/ppsspp/issues/19198 "Prevent a buffer overflow at the end of Atrac tracks."
[#19009]: https://github.com/hrydgard/ppsspp/issues/19009 "More Vulkan barrier code cleanup work"
[#19017]: https://github.com/hrydgard/ppsspp/issues/19017 "Vulkan: More memory barrier simplification and fixes"
[#19018]: https://github.com/hrydgard/ppsspp/issues/19018 "More Vulkan barrier fixes"
[#19560]: https://github.com/hrydgard/ppsspp/issues/19560 "Increase the hardcoded free space reported"
[#19459]: https://github.com/hrydgard/ppsspp/issues/19459 "Fix PSP_STDIN and PSP_MIN_FD value"
[#19340]: https://github.com/hrydgard/ppsspp/issues/19340 "sceIoGetStat: Fix retrieving timestamps from directories"
[#19450]: https://github.com/hrydgard/ppsspp/issues/19450 "Port over LunaMoo's compat flag for The Warriors video playback"
[#18806]: https://github.com/hrydgard/ppsspp/issues/18806 "UFC Undisputed 2010: Crash on device lost on some ARM GPUs"
[#19033]: https://github.com/hrydgard/ppsspp/issues/19033 "Break out the Atrac3/Atrac3+ decoders from FFMPEG to a separate library"
[#19053]: https://github.com/hrydgard/ppsspp/issues/19053 "Remove ffmpeg use from the sceAtrac HLE module"
[#19097]: https://github.com/hrydgard/ppsspp/issues/19097 "implement sysclib_sprintf"
[#19490]: https://github.com/hrydgard/ppsspp/issues/19490 "Fix Z-buffer issue in Socom Fireteam Bravo character customizer, plus a couple of minor things"
[#19364]: https://github.com/hrydgard/ppsspp/issues/19364 "Slightly nudge down the multiplier used for float->u8 conversion in fragment shaders"
[#19489]: https://github.com/hrydgard/ppsspp/issues/19489 "Hardware transform: Clamp the specular coefficient to 0.0 before calling pow()"
[#19439]: https://github.com/hrydgard/ppsspp/issues/19439 "Fix the MGS2 Acid renderpass merge optimization"
[#19122]: https://github.com/hrydgard/ppsspp/issues/19122 "More minor fixes"
[#19334]: https://github.com/hrydgard/ppsspp/issues/19334 "Improved provoking vertex fix"
[#19457]: https://github.com/hrydgard/ppsspp/issues/19457 "Tracing support for the IR Interpreter"
[#19361]: https://github.com/hrydgard/ppsspp/issues/19361 "OpenXR - Anti-flickering rendering flow added"
[#19420]: https://github.com/hrydgard/ppsspp/issues/19420 "OpenXR - Ensure we have a valid poses after app event"
[#19416]: https://github.com/hrydgard/ppsspp/issues/19416 "OpenXR - Hotfix for v69"
[#19389]: https://github.com/hrydgard/ppsspp/issues/19389 "OpenXR - VR camera on any platform"
[#19390]: https://github.com/hrydgard/ppsspp/issues/19390 "OpenXR - Removal of \"VR/Experts only\" section"
[#19092]: https://github.com/hrydgard/ppsspp/issues/19092 "OpenXR - Support for Meta Horizon OS"
[#18924]: https://github.com/hrydgard/ppsspp/issues/18924 "Fix a bunch of cases where we forgot to check for CHD files"
[#19580]: https://github.com/hrydgard/ppsspp/issues/19580 "GCC/llvm: Enable a lot more warnings, error on missing return value"
[#19582]: https://github.com/hrydgard/ppsspp/issues/19582 "Fix minor theme issue in Store"
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
