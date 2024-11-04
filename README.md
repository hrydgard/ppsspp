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

[comment]: # (LINK_LIST_BEGIN_HERE)
[#18228]: https://github.com/hrydgard/ppsspp/issues/18228 "unittest: Add jit compare for jit IR"
[#18235]: https://github.com/hrydgard/ppsspp/issues/18235 "irjit: Handle VDet"
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