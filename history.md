This is history moved out from README.md, which was getting a bit long.


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

What's new in 1.6.3
-------------------
* Crashfixes, task switching and one in Phantasy Star Portable
* Improve graphics in PoP on some devices

What's new in 1.6.1, 1.6.2
--------------------------
* Crashfixes
* Fix broken graphics in flOw.

What's new in 1.6.0
-------------------
* OpenGL backend now properly multithreaded, giving a good speed boost.
* Various Vulkan performance improvements (like [#10911]) and memory allocation fixes.
* GPU command interpreter performance improvements ([#10658])
* Various fixes for app switching and widgets ([#10855]) on Android
* Bugfixes and some performance improvements in the ARM64 JIT compiler and IR interpreter
* Shader cache enabled for Vulkan
* Multiple iOS fixes, including JIT ([#10465]) and file browser ([#10921]).
* Improved compatibility on Mac ([#10113])
* Texture replacement ID bugfix (note: some textures from 1.5.4 may become incompatible)
* Adhoc multiplayer fixes ([#8975])
* Vulkan support on Linux/SDL ([#10413])
* Retroarch support

What's new in 1.5.4
-------------------
* Bugfixes and crashfixes!

What's new in 1.5.0
-------------------
* Full Vulkan support, also for Android now. Very fast on supported devices. ([#10033], [#10049])
* Smarter graphics state management, reduced CPU consumption on all backends ([#9899])
* Android: Support for Arabic and other scripts we couldn't support before
* Fix Android widgets, screen scaling ([#10145])
* Fixes to video dumping
* Geometry problems fixed in Medal of Honor
* Implement immediate draws, fixing Thrillville ([#7459])
* Software rendering improvements, speed and accuracy
* Hardware tesselation of PSP Beziers and Splines (used by a few games)
* Partial sceUsbGps and sceUsbCam support (Android)
* Android "Sustained performance mode" to avoid thermal throttling ([#9901])
* Linux controller mapping fixes ([#9997])
* Assorted bugfixes and compatibility improvements

What's new in 1.4.2
-------------------
* Fixed longstanding bug causing games to crash on ARM64
* Software rendering crashfix, plus hide it for Android users
* D3D9 pixel offset bug fix (blurriness)
* Fixes for homebrew: Timing, MEMSIZE

What's new in 1.4.1
-------------------
* Fixes for some common hangs and crashes ([#9698], ...)
* Vertex decoder optimizations ([#9674])
* Corrections to mipmap bias and selection function ([#9633])
* Major improvements and fixes to software renderer, including mipmap support ([#9635], ...)
* UI background image support
* Basic mouse input support on Windows
* Windows desktop touch fixes ([#9560])
* D3D11 "depal" color fixes, fixing Sonic Rivals
* Fix crash in framebuffer blits affecting Persona 3 ([#9566])

What's new in 1.4-2
-------------------
* Bugfix release - build system didn't copy the flash0 directory to the APK on Android.

What's new in 1.4
-----------------
* Support D3D11 (performs better than OpenGL or D3D9 on most hardware)
* Audio quality improvement (linear interpolation) ([#8950])
* Hardware spline/bezier tesselation in OpenGL, D3D11 and Vulkan (...)
* Post-processing shaders in D3D11
* Prescale UV setting removed, now the default (improves perf) ([#9176])
* High DPI display fixes
* Various fixes for UMD switching for multi-UMD games ([#9245], [#9256])
* New audio setting to improve compatibility with Bluetooth headsets
* Various desktop gamepad compatibility fixes
* Workaround for mipmap issue, fixing fonts in Tactics Ogre Japanese
* Assorted minor compatibility fixes, code cleanup and performance improvements

Support for Symbian, Maemo and Blackberry has been removed.

What's new in 1.3.0
-------------------
* Fix JIT problems on Galaxy S7 and iOS 9+ devices. ([#8965], [#8937])
* Fix Android TV support and use latest FFmpeg. ([#8651], [#8870])
* Texture replacement support - for custom textures and upscaling. ([#8715], [#8821])
* Initial game recording / TAS features. ([#8939], [#8945])
* Correctly map memory on Raspberry Pi 3, much better performance. ([#8801])
* Workaround rendering issues on Tegra K1/X1. (8294a54)
* Disc streaming to play quickly from tablet/phone on wifi. ([#8845])
* Initial Vulkan support - not full featured yet. ([#8601], etc.)
* Experimental new CPU backend and CPU fixes. ([#8725])
* Allow insert/eject of memstick - required by some games. ([#8889])
* Better support for ps3 controller mapping. ([#8949])
* Better UI handling for settings with long names in some languages. ([#8900], [#8898])
* Screenshots in compatibility reporting, better website. ([#8812])
* Fix type D cheat codes, allow for homebrew. ([#8818])
* Graphic glitch fixes in several games. ([#8686], [#8757], [#8804])
* Fix video playback glitches in several games. ([#8793], [#8803], [#8867], [#8914])
* Various performance and compatibility improvements. ([#8868], [#8884], [#8932], [#8934], [#8813], [#8701], [#8960])
* Various debugger and GE debugger improvements. ([#8882], [#8762])
* Fix some problems when switching away from and back to the app ([#8968])

What's new in 1.2.2
-------------------
* Went back to the old way of initializing graphics on Android. Should fix many recent issues.
* Some graphical fixes, a vertex cache performance improvement and a screen clear optimization
* Fix for dual source blending on most SHIELD devices, causing graphical issues.
* Fix the homebrew store incorrectly unzipping some games. This will lead to more games being added.
* Slightly faster ISO handling

What's new in 1.2.1
-------------------
* Fixes for some crash-on-shutdown and app switching problems.

What's new in 1.2.0
-------------------
* A major rework of sceAtrac audio decoding, fixing various music hangs and similar issues
* Many fixes and workarounds to depth and stencil buffer usage, and also FBO management
* Audio reverb support
* Combo keys - custom touch buttons that press multiple PSP buttons
* 5xBR upscaling on GPU (postprocessing effect)
* Fix problems with playback of video with mono audio
* Performance improvements like multithreaded audio mixing
* ARM64 JIT crash bug fixes
* GLSL shader cache to reduce stuttering ingame
* Support render-to-CLUT functionality that some games use to change colors of various monsters
* x86-64 support on Android
* Auto-hide on-screen controls after a while of no usage
* Fixes to prescale UV speedhack, now seems reliable
* Faster ISO RAM cache
* New UI for moving around the PSP display on larger screens
* Minor UI fixes like better slider controls
* Assorted stability fixes (ffmpeg crash, etc)
* Volume setting is back
* Preparations for supporting more graphics APIs
* AdHoc port offset
* Support another HD remaster (Sora no Kiseki Kai HD)

What's new in 1.1.1
-------------------
* Fixed new crash in Persona and other games on ARM64, like Galaxy S6
* Fixed crashes when trying to launch web browser when not present on Android, like on Android TV
* Fix crash in games that used "depal" functionality (OpenGL)
* Fixed rounding mode problems in the JITs
* Fix crash when loading savestates many times, and savestate compatibility on Android-x86
* Fix minor glitch in Ridge Racer

What's new in 1.1
-----------------
* Support for ARM64 on Android, for improved performance on new devices. Has some new optimizations.
* Support Android TV, like nVidia Shield TV
* Screen rotation on PC, useful for vertical games like Star Soldier
* Many minor performance improvements and compatibility bug fixes
* GPU emulation fixes like correct depth rounding, fixing text in Phantasy Star
* Other graphical fixes like UV rotation
* Support savestates for homebrew apps
* Simple integrated "Homebrew Store" to download PSP homebrew apps
* Minor AdHoc multiplayer improvements. Still many issues left.
* Disable a dangerous optimization on ARM, causing walk-through-walls in Tenchu
* sceAtrac music compatibility fixes, fixing noise in a few games
* Better texture scaling performance
* Direct3D closer in features to OpenGL
* Works better on BSD operating systems
* Savedata management UI

What's new in 1.0.1
-------------------
* Bugfixes like the save state scroll issue, cosmetic issues like overscroll
* Some cheat code bugfixes
* Adler32 and Mersenne Twister modules added (fixes some obscure games)
* Fix for Jak & Daxter slowdown
* Graphics hack for Phantasy Star Portable 2 for Direct3D9
* Fix compatibility with some PowerVR devices broken since v0.9.5-959-g4998044

What's new in 1.0
-----------------
* Many, many bug fixes in JIT and elsewhere, improving compatibility
* Proper fix for Zenfone and related devices
* Direct3D 9 supported as a rendering backend on Windows, helps on old GPUs and can be faster than OpenGL in many cases
* You can now create specific configs per game
* FPU rounding modes much better supported, fixes the Peace Walker boss that was undefeatable. NOTE: This breaks saves in Gods Eater - you must turn off the better rounding, load your save game, turn it on and save.
* The JIT now uses SSE on x86, improving speed considerably. This does not affect ARM devices, that's for the next version or two.
* Improved audio output code on both Windows and Android, reducing audio latency on Windows and on some Android systems
* FFMPEG upgraded, fixed some music hangs ("GHA phase shifts")
* Some Ad Hoc improvements, coldbird.net is now default adhoc server. Ad Hoc still unfinished and hard to use.
* Graphics fixes: Bezier/spline drawing fixes, vertex position fixes, DanganRonpa on Adreno fixed, flat shading fixed, vertex cache improved, some PowerVR blockiness issues fixed, screen scaling filter added, Google Cardboard support
* Simulate UMD speed better, fixing hangs in several games
* More Atrac3 fixes, fixing hangs
* Somewhat better disk full handling
* Fixes to dynamic unloading of code, fixing problems in GEB and TRM 2/3
* Updated to SDL2 where applicable (Linux, Mac)
* Some new features, like analog/dpad-swap hotkey, graphics hack for Phantasy Star, show last bit of debug log in dev tools, etc

What's new in 0.9.9.1
-------------------
A few issues have been discovered in the release that need fixing, the Star Ocean fix had a bug and there are some unexpected slowdowns.

Improved sceMpegRingbufferAvailableSize -  UFC© Undisputed​™ 2010 now playable

Improved ISO File System - Bleach Soul Carnival 2 now in-game (but freeze when in menu)

What's new in 0.9.9
-------------------
* CLUT (paletted) texturing from framebuffers supported, fixing many graphical issues
  like the shadows in Final Fantasy: Type-0
* More types of framebuffer copies are now handled correctly, fixing a variety of graphical
  issues, like the sun in Burnout and many more
* Better savedata compatibility with the real PSP
* Support for more codecs used by "Custom BGM" and sometimes regular music in games: MP3, AAC
* PMP video format support
* Implemented some strange blending modes like ABSDIFF as shaders, fixing the outlines in DBZ Tag Team and more.
* Emulation of the vrot CPU instruction improved - it caused cracks in FF3 graphics before
* Many bugfixes around the UI, touch D-pad now works better when scaled large
* Workaround implemented to support Star Ocean's stencil trickery as efficiently as possible on all platforms
* Major corrections to module loading and memory management, fixing further games
* Bulgarian and Thai language translations were contributed
* Many, many more game fixes and bug fixes

What's new in 0.9.8
-------------------
* OpenGL ES 3 detection bug on Xperia devices fixed, graphics work again.
* More accurate audio mixing and emulation
* Software rendering and display list performance improvements
* Workaround for timing issue hanging Crash Tag Team Racing
* Galician language
* Built-in ARM disassembler improvements (dev feature)
* Fix for immersive mode volume key issue on Android Kitkat
* And more minor tweaks and fixes as always.

What's new in 0.9.7.1
---------------------
* Some critical bugfixes (rotation, haptic feedback on Android, etc)

What's new in 0.9.7
-------------------
* Several scheduling and audio fixes, fixing black screens in Yu Gi Oh games among other things.
* Screen rotation and immersive mode support on Android
* Large improvements to the software renderer (still not really playable, but looks right more
  often than not)
* New VPL allocator and bugfixes, fixing Pangya Golf performance problems.
* Some mpeg/video playback fixes, fixing Parappa The Rapper and others. Some issues remain.
* Fix save state bugs causing incompatibility between 32 and 64-bit platforms.
* Symbol map/debugger improvements
* Depth buffer copy, fixing Jeanne D'arc. May cause minor slowdowns though, this will be worked
  around in the future.
* MsgDialog fixes. Saving fixed in numerous games.
* Initial multitouch support on Windows 8 for on-screen controls.

What's new in 0.9.6
-------------------
* Large general speed improvements and assorted bug fixes
* "Software Skinning" option which speeds up many games with animated 3D characters
  (but may slow down a few, like Monster Hunter games - experiment with turning it off)
* Various fixes around stencil/alpha, reducing glow problems in Wipeout and Gods Eater Burst.
* Timing improvements making more games run at the correct FPS, also fixing some audio issues
* More debugger features
* Option for four-way touch dpad, avoiding diagonal dpad issues
* Better looking and individually resizable touch controls
* Add ability to switch UMD in multi-disc games (works for most)
* Emulate PSP-2000 rather than the 1000 model by default. Not much different in practice.
* Automatic install of games from ZIP files, like demos and many homebrew.
* VERY basic ad-hoc online play support, to be improved in future versions. See below.
* Software renderer improvements

What's new in 0.9.5
-------------------
* Many, many emulation fixes:
  - bezier/spline curve support, fixing LocoRoco and others
  - stencil clear emulation, fixing Final Fantasy IV text
* Performance improvements in some games
* Post-processing shaders like FXAA, scanlines, vignette
* More solid save states (we will try to keep them working from now on. Save states only upgrade forward,
  not backward to older versions though).
* Change render resolution independently of window size
* Massive debugger improvements
* Win32 menu bar is now translatable
* Multiple UI bugs were fixed, and the UI instantly changes when a new language is selected
* Win32: Ability to store PPSSPP's config files and memory stick files in places other than the same directory
* Android-x86 support
* Unofficial port for modified Xbox 360 consoles
* Atrac3+ plugin no longer required. Symbian now supports Atrac3+ audio.
* Symbian audio and ffmpeg is now threaded for more consistent media processing.
* Haptic feedback support for mobile devices.
* Accurate system information for mobile devices.
* Qt audio has been fixed.
* Analog controller support for Blackberry.


ADHOC SUPPORT (by Igor Calabria)
================================
This is based on coldbird's code: https://code.google.com/archive/p/aemu/
All credit goes to him!

Status
------
Code is a bit of a mess and it's not fully functional yet, I still need to implement
some functions and add a upnp lib(really important for people with routers).

I did test it with some games(emulator <-> real psp with the server running locally)
and it's looking good:

* Worms Open Warfare: Ran just fine, I was able to play a whole match without problems
* Monster Hunter Freedom Unite: Runs fine too. Gathering Hall and embarking on quests works
* Dissidia Duodecim 012: Doesn't work. It requires some functions that I haven't implemented
yet. Also, it uses a port < 1000 and thats reserved for admin apps on linux, running the emu
as sudo "solves" it, but it's far from ideal.
* Pacman World Rally: Works too.

Update (Kyhel) :
---------------
* Now compiles on both Mac OSX and Windows. For more details on how to play and build
go to see there http://forums.ppsspp.org/showthread.php?tid=3595&pid=59241#pid59241
* Got it tested windows <-> mac osx <-> psp, it works
* Monster Hunter 3rd HD works too, as well as God Eater Burst.


[comment]: # (LINK_LIST_BEGIN_HERE)
[#10911]: https://github.com/hrydgard/ppsspp/issues/10911 "Vulkan: Depalettize in shaders"
[#10658]: https://github.com/hrydgard/ppsspp/issues/10658 "Execute_Prim: Add a smaller \"inner interpreter\" to speed long up sequences of PRIM commands"
[#10855]: https://github.com/hrydgard/ppsspp/issues/10855 "PPSSPP 1.5.4 on Android. Shortcuts do not work the first time."
[#10465]: https://github.com/hrydgard/ppsspp/issues/10465 "Allow arm64 jit when memory base is not nicely aligned"
[#10921]: https://github.com/hrydgard/ppsspp/issues/10921 "Add support for File app (file browser) on iOS 11"
[#10113]: https://github.com/hrydgard/ppsspp/issues/10113 "Request a core profile in OpenGL (after 1.5.0)"
[#8975]: https://github.com/hrydgard/ppsspp/issues/8975 "Ad hoc: Band-aid fix for clear peer list. Should fix #7331"
[#10413]: https://github.com/hrydgard/ppsspp/issues/10413 "SDL/X11 Vulkan init"
[#10033]: https://github.com/hrydgard/ppsspp/issues/10033 "VulkanRenderManager - big refactoring of the Vulkan code"
[#10049]: https://github.com/hrydgard/ppsspp/issues/10049 "Vulkan threading tweaks and minor"
[#9899]: https://github.com/hrydgard/ppsspp/issues/9899 "Vulkan state optimizations"
[#10145]: https://github.com/hrydgard/ppsspp/issues/10145 "Android: When creating shortcuts, put the file path in data, not in extras."
[#7459]: https://github.com/hrydgard/ppsspp/issues/7459 "Thrillville Off The Rails Graphics' issues"
[#9901]: https://github.com/hrydgard/ppsspp/issues/9901 "Initial work on supporting sustained perf mode"
[#9997]: https://github.com/hrydgard/ppsspp/issues/9997 "Create default mapping for unknown control pads in SDL's controller database"
[#9698]: https://github.com/hrydgard/ppsspp/issues/9698 "General crash and hang fixes"
[#9674]: https://github.com/hrydgard/ppsspp/issues/9674 "Implement a few x86 vertexjit optimizations"
[#9633]: https://github.com/hrydgard/ppsspp/issues/9633 "Improve support for mipmaps and related headless fixes"
[#9635]: https://github.com/hrydgard/ppsspp/issues/9635 "SoftGPU: Rasterize triangles in chunks of 4 pixels"
[#9560]: https://github.com/hrydgard/ppsspp/issues/9560 "Allow using any touchId to scroll. Should help #9554."
[#9566]: https://github.com/hrydgard/ppsspp/issues/9566 "Fix out-of-bounds framebuffer blit on color bind"
[#8950]: https://github.com/hrydgard/ppsspp/issues/8950 "SasAudio: Implement linear interpolation"
[#9176]: https://github.com/hrydgard/ppsspp/issues/9176 "Always prescale uv (\"texcoord speedhack\")"
[#9245]: https://github.com/hrydgard/ppsspp/issues/9245 "Fix \"Akaya Akashiya Ayakashi\" umd switch"
[#9256]: https://github.com/hrydgard/ppsspp/issues/9256 "Fix Dies irae Amantes amentes umd switch"
[#8965]: https://github.com/hrydgard/ppsspp/issues/8965 "Port over the Exynos cacheline size fix from Dolphin."
[#8937]: https://github.com/hrydgard/ppsspp/issues/8937 "Implement W^X-compatible JIT. Hopefully makes JIT work on iOS again"
[#8651]: https://github.com/hrydgard/ppsspp/issues/8651 "Switch Android build to using clang (needs buildbot update)"
[#8870]: https://github.com/hrydgard/ppsspp/issues/8870 "Update to target Android SDK platform 24 (N)"
[#8715]: https://github.com/hrydgard/ppsspp/issues/8715 "Initial texture replacement support"
[#8821]: https://github.com/hrydgard/ppsspp/issues/8821 "Add basic TextureReplacement UI options."
[#8939]: https://github.com/hrydgard/ppsspp/issues/8939 "Add Frame Advance"
[#8945]: https://github.com/hrydgard/ppsspp/issues/8945 "Add Display Recording and Audio Dumping to Desktop"
[#8801]: https://github.com/hrydgard/ppsspp/issues/8801 "Improve performance on Raspberry Pi (and maybe other Linux)"
[#8845]: https://github.com/hrydgard/ppsspp/issues/8845 "Add UI for wifi remote disc streaming"
[#8601]: https://github.com/hrydgard/ppsspp/issues/8601 "Vulkan rendering backend. Early Work-In-Progress"
[#8725]: https://github.com/hrydgard/ppsspp/issues/8725 "IR Interpreter"
[#8889]: https://github.com/hrydgard/ppsspp/issues/8889 "Initial support for memstick insert/remove"
[#8949]: https://github.com/hrydgard/ppsspp/issues/8949 "Adds --PS3 comand line option for PS3 controller support on SDL."
[#8900]: https://github.com/hrydgard/ppsspp/issues/8900 "Wrap text in many places where it makes sense"
[#8898]: https://github.com/hrydgard/ppsspp/issues/8898 "UI: Scale option text down when there's no space"
[#8812]: https://github.com/hrydgard/ppsspp/issues/8812 "Include screenshots in compatibility reports"
[#8818]: https://github.com/hrydgard/ppsspp/issues/8818 "CWC improvements/fixes"
[#8686]: https://github.com/hrydgard/ppsspp/issues/8686 "Interpolate bezier patch colors/UVs using bernstein"
[#8757]: https://github.com/hrydgard/ppsspp/issues/8757 "Save FBOs on decimate when a safe size is known"
[#8804]: https://github.com/hrydgard/ppsspp/issues/8804 "Download single-use renders right away"
[#8793]: https://github.com/hrydgard/ppsspp/issues/8793 "Correct some scePsmf info retrieval funcs and error handling"
[#8803]: https://github.com/hrydgard/ppsspp/issues/8803 "Reject invalid MPEG puts for certain lib versions"
[#8867]: https://github.com/hrydgard/ppsspp/issues/8867 "Improve mpeg parsing / corruption issues"
[#8914]: https://github.com/hrydgard/ppsspp/issues/8914 "Psmf: Ignore stream size with old PsmfPlayer libs"
[#8868]: https://github.com/hrydgard/ppsspp/issues/8868 "Return error when trying to close files with pending operations"
[#8884]: https://github.com/hrydgard/ppsspp/issues/8884 "Better support zh_gb.pgf"
[#8932]: https://github.com/hrydgard/ppsspp/issues/8932 "Font: Draw nothing for chars before first glyph"
[#8934]: https://github.com/hrydgard/ppsspp/issues/8934 "Io: Do not delay on file seek"
[#8813]: https://github.com/hrydgard/ppsspp/issues/8813 "Enable extra ram for The Elder Scrolls Travels Oblivion USA Beta and Melodie alpha"
[#8701]: https://github.com/hrydgard/ppsspp/issues/8701 "Correct dependency handling when loading modules"
[#8960]: https://github.com/hrydgard/ppsspp/issues/8960 "Fix scePowerSetClockFrequency timing"
[#8882]: https://github.com/hrydgard/ppsspp/issues/8882 "Add custom log expressions to the debugger"
[#8762]: https://github.com/hrydgard/ppsspp/issues/8762 "Add more GE debugger features"
[#8968]: https://github.com/hrydgard/ppsspp/issues/8968 "Android: Add both a lost and restore phase"
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
[#13023]: https://github.com/hrydgard/ppsspp/issues/13023 "Add sysclib_strstr"
[#13038]: https://github.com/hrydgard/ppsspp/issues/13038 "Add sysclib_strncmp,sysclib_memmove"
[#13004]: https://github.com/hrydgard/ppsspp/issues/13004 "Warp some ThreadManForKernel and sceKernelExitVSHKernel"
[#13026]: https://github.com/hrydgard/ppsspp/issues/13026 "Add some ThreadManForKernel nids"
[#12225]: https://github.com/hrydgard/ppsspp/issues/12225 "Rebased: Wrap some SysMemForKernel's nids, fixing #7960"
[#13028]: https://github.com/hrydgard/ppsspp/issues/13028 "Real support Code Geass: Lost Colors chinese patched version"
[#13007]: https://github.com/hrydgard/ppsspp/issues/13007 "Real support \"Hatsune Miku Project Diva Extend\" chinese patched version"
[#12817]: https://github.com/hrydgard/ppsspp/issues/12817 "Unification of the menu of Linux and Windows versions"
[#12612]: https://github.com/hrydgard/ppsspp/issues/12612 "SDL analog mouse input"
[#12623]: https://github.com/hrydgard/ppsspp/issues/12623 "Add toggle flag to combo button"
[#12660]: https://github.com/hrydgard/ppsspp/issues/12660 "GPU: Add setting to control inflight frame usage"
[#12637]: https://github.com/hrydgard/ppsspp/issues/12637 "Region flag and game ID on game selection screen"
[#12646]: https://github.com/hrydgard/ppsspp/issues/12646 "Resizable game icons"
[#12643]: https://github.com/hrydgard/ppsspp/issues/12643 "Toggle mute button"
[#12601]: https://github.com/hrydgard/ppsspp/issues/12601 "Add rapid fire to touch control"
[#12552]: https://github.com/hrydgard/ppsspp/issues/12552 "Qt/macOS: enable HiDPI ( retina display ) support"
[#12517]: https://github.com/hrydgard/ppsspp/issues/12517 "Touch control grid snap"
[#12749]: https://github.com/hrydgard/ppsspp/issues/12749 "Auto rotating analog"
[#12756]: https://github.com/hrydgard/ppsspp/issues/12756 "Skip deadzone option on tilt"
[#12818]: https://github.com/hrydgard/ppsspp/issues/12818 "Add option to prevent Mipmaps from being dumped"
[#12486]: https://github.com/hrydgard/ppsspp/issues/12486 "Rebindable touch right analog"
[#12718]: https://github.com/hrydgard/ppsspp/issues/12718 "Vpl: Correct allocation order when splitting block"
[#12746]: https://github.com/hrydgard/ppsspp/issues/12746 "GPU: Assume a scissor of 481x273 is a mistake"
[#12873]: https://github.com/hrydgard/ppsspp/issues/12873 "Vulkan: Framebuffer manager: Use an allocator for \"MakePixelTexture\" images."
[#12898]: https://github.com/hrydgard/ppsspp/issues/12898 "[Android] [Mali GPU] [OpenGL] Lastest build blackscreen on buffered rendering mode"
[#12443]: https://github.com/hrydgard/ppsspp/issues/12443 "Darkstalkers Chronicle: Add specializations and speedhacks to get it kinda playable"
[#12607]: https://github.com/hrydgard/ppsspp/issues/12607 "QT API for camera (Linux/macOS)"
[#12580]: https://github.com/hrydgard/ppsspp/issues/12580 "Add camera support for linux (V4L2)"
[#12572]: https://github.com/hrydgard/ppsspp/issues/12572 "Add camera support for windows."
[#12602]: https://github.com/hrydgard/ppsspp/issues/12602 "Add option to improve frame pacing through duplicate frames if below 60hz."
[#12659]: https://github.com/hrydgard/ppsspp/issues/12659 "Support vsync in all hardware backends, support runtime update"
[#12474]: https://github.com/hrydgard/ppsspp/issues/12474 "Egl bug on rpi4 with master mesa?"
[#12583]: https://github.com/hrydgard/ppsspp/issues/12583 "macOS: Initial support for vulkan on macOS ( MoltenVK )"
[#12519]: https://github.com/hrydgard/ppsspp/issues/12519 "Ghost In The Shell - Stand Alone Complex (ULUS10020) - Black Textures and missing screens."
[#12876]: https://github.com/hrydgard/ppsspp/issues/12876 "Windows: Add safety checks to WASAPI code"
[#12908]: https://github.com/hrydgard/ppsspp/issues/12908 "Fix \"Improved compatibility of sceGeListEnQueue: verify that stackDepth < 256\""
[#8714]: https://github.com/hrydgard/ppsspp/issues/8714 "Allow > 5x PSP resolution for devices like iPad Pro 12.9"
[#12449]: https://github.com/hrydgard/ppsspp/issues/12449 "Reintroduce Cardboard VR"
[#12912]: https://github.com/hrydgard/ppsspp/issues/12912 "Add CWCHEAT for postprocessing"
[#12816]: https://github.com/hrydgard/ppsspp/issues/12816 "Implement Xinput vibration CWCheat (PPSSPP specific 0xA code type)"
[#12901]: https://github.com/hrydgard/ppsspp/issues/12901 "Post shader setting uniform"
[#12905]: https://github.com/hrydgard/ppsspp/issues/12905 "Allow chained post-processing shaders"
[#12667]: https://github.com/hrydgard/ppsspp/issues/12667 "Chat feature based on Adenovan's Rechat branch"
[#12702]: https://github.com/hrydgard/ppsspp/issues/12702 "PPGe: Use TextDrawer for save UI if available"
[#12920]: https://github.com/hrydgard/ppsspp/issues/12920 "Remove the Audio Resampling setting (now always on)."
[#12916]: https://github.com/hrydgard/ppsspp/issues/12916 "More audio buffering fixes (primarily affects SDL)"
[#12685]: https://github.com/hrydgard/ppsspp/issues/12685 "UI: Simple joystick navigation. Fixes #10996."
[#12779]: https://github.com/hrydgard/ppsspp/issues/12779 "Support drawing around notches on Android displays. Fixes #12261"
[#12652]: https://github.com/hrydgard/ppsspp/issues/12652 "ScanForFunctions: Speed up game loading"
[#12462]: https://github.com/hrydgard/ppsspp/issues/12462 "Vulkan: Enable renderpass merging for all games"
[#12695]: https://github.com/hrydgard/ppsspp/issues/12695 "New heuristic for getting rid of unnecessary \"antialias-lines\"."
[#11898]: https://github.com/hrydgard/ppsspp/issues/11898 "Strike Witches - Hakugin no Tsubasa  missing intro video"
[#12941]: https://github.com/hrydgard/ppsspp/issues/12941 "Vulkan: Deal with the reformat clear better"
[#12857]: https://github.com/hrydgard/ppsspp/issues/12857 "Mumbo Jumbo games freeze on loading screen since v1.6"
[#12635]: https://github.com/hrydgard/ppsspp/issues/12635 "Kernel: Delay better in sceKernelReferThreadStatus"
[#12670]: https://github.com/hrydgard/ppsspp/issues/12670 "Attempts to replace 0 frame width with valid frame width.(sceMpegAvcCsc)"
[#12800]: https://github.com/hrydgard/ppsspp/issues/12800 "x86jit: Force INF * 0 to +NAN"
[#13071]: https://github.com/hrydgard/ppsspp/issues/13071 "D3D9: Fix a sign mistake generating the projection matrix."
[#13076]: https://github.com/hrydgard/ppsspp/issues/13076 "Add some excessive null checks to GameScreen::render()"
[#13057]: https://github.com/hrydgard/ppsspp/issues/13057 "The 1.10 Android mystery crash thread!"
[#13077]: https://github.com/hrydgard/ppsspp/issues/13077 "SaveState: Make sure to default init net data"
[#13096]: https://github.com/hrydgard/ppsspp/issues/13096 "Revert \"Remove the I/O on Thread option - treat it as always on.\""
[#13095]: https://github.com/hrydgard/ppsspp/issues/13095 "http: Check cancel flag more often"
[#13093]: https://github.com/hrydgard/ppsspp/issues/13093 "Add a try/catch to Android camera device listing."
[#13094]: https://github.com/hrydgard/ppsspp/issues/13094 "Camera initialization crash fix"
[#13125]: https://github.com/hrydgard/ppsspp/issues/13125 "Refactor and fix touch control layout screen for notch"
[#13633]: https://github.com/hrydgard/ppsspp/issues/13633 "Downstream Panic (US) New Game crashes"
[#13969]: https://github.com/hrydgard/ppsspp/issues/13969 "Io: Don't allow async close while async busy"
[#13368]: https://github.com/hrydgard/ppsspp/issues/13368 "Reschedule after resuming thread from suspend."
[#13204]: https://github.com/hrydgard/ppsspp/issues/13204 "Dissidia Final Fantasy Chinese patch invalid address"
[#8526]: https://github.com/hrydgard/ppsspp/issues/8526 "Xyanide Resurrection freezing"
[#4671]: https://github.com/hrydgard/ppsspp/issues/4671 "Capcom Classic Collection Reloaded stuck in return game"
[#6025]: https://github.com/hrydgard/ppsspp/issues/6025 "Dream Club Portable crash after select girl"
[#13298]: https://github.com/hrydgard/ppsspp/issues/13298 "Fix sceKernelExitThread"
[#13297]: https://github.com/hrydgard/ppsspp/issues/13297 "Namco Museum Vol. 2 - ULJS00047 infinite loading in some game"
[#9523]: https://github.com/hrydgard/ppsspp/issues/9523 "Namco Museum - Battle Collection - ULUS100035 loading problem"
[#5213]: https://github.com/hrydgard/ppsspp/issues/5213 "Hanayaka Nari Wa ga Ichizoku strange MP3 mono voice"
[#12761]: https://github.com/hrydgard/ppsspp/issues/12761 "[Android][OpenGL&Vulkan][Save issue] Assassin's Creed : Bloodlines (ULJM05571)"
[#13335]: https://github.com/hrydgard/ppsspp/issues/13335 "Support for loading game-specific plugins"
[#12924]: https://github.com/hrydgard/ppsspp/issues/12924 "Postprocessing: User chain support"
[#13654]: https://github.com/hrydgard/ppsspp/issues/13654 "Expose the \"Lower resolution for effects\" setting in libretro."
[#13695]: https://github.com/hrydgard/ppsspp/issues/13695 "Add developer setting \"Show on-screen messages\". Uncheck to hide them."
[#13866]: https://github.com/hrydgard/ppsspp/issues/13866 "SaveState: Allow rewind on mobile"
[#13355]: https://github.com/hrydgard/ppsspp/issues/13355 "Refactor framebuffer attachment. Fixes Test Drive Unlimited performance"
[#13556]: https://github.com/hrydgard/ppsspp/issues/13556 "D3D11 depth texture support"
[#13262]: https://github.com/hrydgard/ppsspp/issues/13262 "Implement texturing from depth buffers (Vulkan only so far)"
[#13986]: https://github.com/hrydgard/ppsspp/issues/13986 "Vulkan: Add MMPX upscaling texture shader"
[#13514]: https://github.com/hrydgard/ppsspp/issues/13514 "Vulkan: Automatically generate mipmaps for replaced/scaled textures"
[#13235]: https://github.com/hrydgard/ppsspp/issues/13235 "Vulkan: Allow custom texture upscaling shaders"
[#13526]: https://github.com/hrydgard/ppsspp/issues/13526 "VFPU: Compute sines and cosines in double precision."
[#13706]: https://github.com/hrydgard/ppsspp/issues/13706 "Add back the old implementation of vfpu_sin/cos/sincos."
[#12336]: https://github.com/hrydgard/ppsspp/issues/12336 "Microphone support"
[#13792]: https://github.com/hrydgard/ppsspp/issues/13792 "Fix UWP audio and a hang bug"
[#13239]: https://github.com/hrydgard/ppsspp/issues/13239 "Add sound effects for PPSSPP interface navigation"
[#13897]: https://github.com/hrydgard/ppsspp/issues/13897 "LittleBigPlanet - Game Not Loading, Blue Screen (iOS, Unplayable)"
[#13172]: https://github.com/hrydgard/ppsspp/issues/13172 "Generalized multi-instance"
[#13092]: https://github.com/hrydgard/ppsspp/issues/13092 "Bad memory access handling improvements"
[#11795]: https://github.com/hrydgard/ppsspp/issues/11795 "Exception handler - catch bad memory accesses"
[#7124]: https://github.com/hrydgard/ppsspp/issues/7124 "Yu-Gi-Oh! GX Tag Force Card summoning (card cut-off / cropped)"
[#13760]: https://github.com/hrydgard/ppsspp/issues/13760 "Fix car lighting issues in DiRT 2."
[#13640]: https://github.com/hrydgard/ppsspp/issues/13640 "Fix car reflections in Outrun"
[#13636]: https://github.com/hrydgard/ppsspp/issues/13636 "Reinterpret framebuffer formats as needed. Outrun reflections partial fix"
[#14117]: https://github.com/hrydgard/ppsspp/issues/14117 "[Adhoc] Fix lob"
[#14106]: https://github.com/hrydgard/ppsspp/issues/14106 "[Adhoc] Fix frozen (0 FPS) issue on Kao Challengers and Asterix & Obelix XX"
[#14078]: https://github.com/hrydgard/ppsspp/issues/14078 "PPGe: Decimate text images properly"
[#14101]: https://github.com/hrydgard/ppsspp/issues/14101 "Menu audio glitchfix"
[#14092]: https://github.com/hrydgard/ppsspp/issues/14092 "SaveState: Prevent crash on bad cookie marker"
[#14091]: https://github.com/hrydgard/ppsspp/issues/14091 "Only allow sceMpegGetAvcAu warmup for God Eater Series"
[#14089]: https://github.com/hrydgard/ppsspp/issues/14089 "GL: Call CreateDeviceObjects *after* updating render_."
[#14085]: https://github.com/hrydgard/ppsspp/issues/14085 "Handle exec addr errors better - don't let IgnoreBadMemoryAccesses skip dispatcher exceptions"
[#14132]: https://github.com/hrydgard/ppsspp/issues/14132 "Io: Truncate reads/writes to valid memory"
[#14134]: https://github.com/hrydgard/ppsspp/issues/14134 "Android: Ensure shutdown waits for render"
[#14129]: https://github.com/hrydgard/ppsspp/issues/14129 "GPU: Force reinterpret off without copy image"
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
[#13252]: https://github.com/hrydgard/ppsspp/issues/13252 "Open Season Title Screen does not display"
[#14127]: https://github.com/hrydgard/ppsspp/issues/14127 "Metal Gear Solid Peace Walker Chinese Patched blue screen"
[#14271]: https://github.com/hrydgard/ppsspp/issues/14271 "Ape Academy 2 is broken on versions after 1.8.0(?) - tested on latest nightly and 1.11.3"
[#14789]: https://github.com/hrydgard/ppsspp/issues/14789 "Add new texture filtering mode \"Auto Max Quality\""
[#13741]: https://github.com/hrydgard/ppsspp/issues/13741 "Princess Maker 5 Portable half screen in Vulkan"
[#14167]: https://github.com/hrydgard/ppsspp/issues/14167 "[Android] Pro Yakyu Spirits 2010 (NPJH50234): Rendering errors with hardware transform off"
[#14230]: https://github.com/hrydgard/ppsspp/issues/14230 "Support texture replacement filtering overrides"
[#13759]: https://github.com/hrydgard/ppsspp/issues/13759 "Yarudora Portable: Double Cast"
[#13146]: https://github.com/hrydgard/ppsspp/issues/13146 "Sims 2 Castaway/Pets EA Logo glitched out - 1.10.2"
[#14209]: https://github.com/hrydgard/ppsspp/issues/14209 "Fix Size & Position jpn0.pgf/ltn0.pgf/ltn2.pgf/ltn4.pgf/ltn6.pgf"
[#14042]: https://github.com/hrydgard/ppsspp/issues/14042 "Implement basic depth texturing for OpenGL"
[#14966]: https://github.com/hrydgard/ppsspp/issues/14966 "Config: Correct cardboard setting ini load"
[#14768]: https://github.com/hrydgard/ppsspp/issues/14768 "Fix the math in cardboard VR mode for wide aspect ratios"
[#14069]: https://github.com/hrydgard/ppsspp/issues/14069 "Mini-Map in Z.H.P. Updates Incorrectly Without Software Rendering"
[#14879]: https://github.com/hrydgard/ppsspp/issues/14879 "vertexjit: Correct morph flag alpha check assert"
[#14711]: https://github.com/hrydgard/ppsspp/issues/14711 "Sas: Add option to control reverb volume"
[#14313]: https://github.com/hrydgard/ppsspp/issues/14313 "Add a setting for choosing background animation in PPSSPP's menus"
[#14818]: https://github.com/hrydgard/ppsspp/issues/14818 "Focus based moving background"
[#14810]: https://github.com/hrydgard/ppsspp/issues/14810 "Wave animation"
[#14347]: https://github.com/hrydgard/ppsspp/issues/14347 "UI: Add BG animation for recent games"
[#14000]: https://github.com/hrydgard/ppsspp/issues/14000 "Add CRC32 calc"
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
[#11974]: https://github.com/hrydgard/ppsspp/issues/11974 "[Linux] [Vulkan] Toggle fullscreen doesn't update display properly"
[#16710]: https://github.com/hrydgard/ppsspp/issues/16710 "GLES: Use uint for uint shift amounts"
[#16709]: https://github.com/hrydgard/ppsspp/issues/16709 "Correct some shader errors in reporting"
[#16708]: https://github.com/hrydgard/ppsspp/issues/16708 "Additional fixes from both our reports and Play reports"
[#16703]: https://github.com/hrydgard/ppsspp/issues/16703 "Minor fixes based on the latest Google Play report"
[#16706]: https://github.com/hrydgard/ppsspp/issues/16706 "softgpu: Fix lighting with 0 exp"
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
[#16645]: https://github.com/hrydgard/ppsspp/issues/16645 "Fix vertex shader range culling - the driver bug check was wrong."
[#16656]: https://github.com/hrydgard/ppsspp/issues/16656 "Fix rendering of lines with the same x/y but different z."
[#16673]: https://github.com/hrydgard/ppsspp/issues/16673 "Show bluescreen properly on memory errors that we failed to ignore."
[#16662]: https://github.com/hrydgard/ppsspp/issues/16662 "Correct some reversed dependencies, minor other cleanup"
[#16655]: https://github.com/hrydgard/ppsspp/issues/16655 "OpenXR - Force flat mode for Madden NFL games"
[#16644]: https://github.com/hrydgard/ppsspp/issues/16644 "Additional Android cleanup"
[#16636]: https://github.com/hrydgard/ppsspp/issues/16636 "Crash: Ensure we never handle faults in faults"
[#16639]: https://github.com/hrydgard/ppsspp/issues/16639 "Minor initialization cleanup, setup for Vulkan validation layers on Android"
[#16599]: https://github.com/hrydgard/ppsspp/issues/16599 "Vulkan: Remove the new 0th descriptor set, move everything else back to desc set 0"
[#16614]: https://github.com/hrydgard/ppsspp/issues/16614 "GPU: Keep prevPrim_ set on flush"
[#16617]: https://github.com/hrydgard/ppsspp/issues/16617 "GE Debugger: Prevent double init"
[#16609]: https://github.com/hrydgard/ppsspp/issues/16609 "OpenXR - Rendering fixes for a few games"
[#16608]: https://github.com/hrydgard/ppsspp/issues/16608 "Cleanup value corrections in config load/save"
[#16615]: https://github.com/hrydgard/ppsspp/issues/16615 "D3D9: Support old-style user clip planes"
[#16619]: https://github.com/hrydgard/ppsspp/issues/16619 "Debugger: Don't hang memory dump if stepping in GE"
[#16595]: https://github.com/hrydgard/ppsspp/issues/16595 "Transparent background option"
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
[#15738]: https://github.com/hrydgard/ppsspp/issues/15738 "Add confirmation dialog when generating a new Mac address"
[#15739]: https://github.com/hrydgard/ppsspp/issues/15739 "irjit: Correct another PurgeTemps case"
[#15740]: https://github.com/hrydgard/ppsspp/issues/15740 "Replacement: Clear cache on disable"
[#15734]: https://github.com/hrydgard/ppsspp/issues/15734 "Better pt-pt translation"
[#15733]: https://github.com/hrydgard/ppsspp/issues/15733 "Fix bug in blue-to-alpha - alpha blending could be on when it shouldn't be."
[#15237]: https://github.com/hrydgard/ppsspp/issues/15237 "Path: Check for PSP case insensitively"
[#15487]: https://github.com/hrydgard/ppsspp/issues/15487 "Save textures on background tasks when texture dumping is enabled."
[#15645]: https://github.com/hrydgard/ppsspp/issues/15645 "UI: Add analog speed limit mapping"
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
[#15566]: https://github.com/hrydgard/ppsspp/issues/15566 "Screen upscaling shaders improvements"
[#15238]: https://github.com/hrydgard/ppsspp/issues/15238 "Vulkan: Be more restrictive about hardware texture upscaling on \"slow\" GPUs"
[#15217]: https://github.com/hrydgard/ppsspp/issues/15217 "Vulkan is strict about scissor rect, so let's clamp centrally."
[#15211]: https://github.com/hrydgard/ppsspp/issues/15211 "Vulkan: Specify Vulkan version, fix mip level generation calculation"
[#15162]: https://github.com/hrydgard/ppsspp/issues/15162 "Integrate VMA (Vulkan Memory Allocator)"
[#15106]: https://github.com/hrydgard/ppsspp/issues/15106 "GLES: Explicitly enable ARB_cull_distance"
[#15025]: https://github.com/hrydgard/ppsspp/issues/15025 "Allow delayed loading of texture replacements"
[#15470]: https://github.com/hrydgard/ppsspp/issues/15470 "Threading manager stresstest and fixes"
[#14414]: https://github.com/hrydgard/ppsspp/issues/14414 "Add search for settings"
[#15613]: https://github.com/hrydgard/ppsspp/issues/15613 "Allow to repeat a \"single\" button"
[#15569]: https://github.com/hrydgard/ppsspp/issues/15569 "Upscaling shaders"
[#15494]: https://github.com/hrydgard/ppsspp/issues/15494 "Add key bind to hotswap internal screen rotation"
[#15510]: https://github.com/hrydgard/ppsspp/issues/15510 "Allow to set InternalScreenRotation per game"
[#12958]: https://github.com/hrydgard/ppsspp/issues/12958 "Feature Request: restore software rendering ui setting on android"
[#15016]: https://github.com/hrydgard/ppsspp/issues/15016 "List of games with graphics rendering issues using mali gpu with vulkan backend"
[#15640]: https://github.com/hrydgard/ppsspp/issues/15640 "Disable ForceMax60FPS for GOW games and replace it with fixed 60 fps"
[#15213]: https://github.com/hrydgard/ppsspp/issues/15213 "[Adhoc] Updated PdpCreate, PdpSend, PdpRecv, GetPdpStat, GetPtpStat"
[#15215]: https://github.com/hrydgard/ppsspp/issues/15215 "[Adhocctl] Fix Tekken 5 Dark Resurrection Multiplayer"
[#15549]: https://github.com/hrydgard/ppsspp/issues/15549 "GPU: Hook Sol Trigger func to flush texture"
[#15583]: https://github.com/hrydgard/ppsspp/issues/15583 "Fix and further improve line drawing in Echochrome"
[#15073]: https://github.com/hrydgard/ppsspp/issues/15073 "Cleanup line/point handling and refactor a bit"
[#15075]: https://github.com/hrydgard/ppsspp/issues/15075 "Draw points using triangles"
[#15500]: https://github.com/hrydgard/ppsspp/issues/15500 "Add BlueToAlpha compat.ini workaround, fixes Split/Second graphics"
[#15501]: https://github.com/hrydgard/ppsspp/issues/15501 "Make the existing ReinterpretFramebuffers/ShaderColorBitmask path work for Split/Second"
[#15691]: https://github.com/hrydgard/ppsspp/issues/15691 "Add a simple compat flag to workaround the Clone Wars issue, #12949"
[#15696]: https://github.com/hrydgard/ppsspp/issues/15696 "Use the recent Clone Wars fix for Star Wars: Force Unleashed too"
[#12949]: https://github.com/hrydgard/ppsspp/issues/12949 "Star Wars: The Clone Wars - Graphic glitch [Android/Windows]"
[#9572]: https://github.com/hrydgard/ppsspp/issues/9572 "Star Wars force unleashed [Screen Overlay problem]"
[#15687]: https://github.com/hrydgard/ppsspp/issues/15687 "Add Zettai Zetsumei Toshi 3"
[#7295]: https://github.com/hrydgard/ppsspp/issues/7295 "Juiced 2: Hot Import Nights, screen artifacts and missing half of race tracks"
[#15717]: https://github.com/hrydgard/ppsspp/issues/15717 "Allows \"merging\" render targets that overlap on the Y axis. Fixes Juiced 2"
[#15698]: https://github.com/hrydgard/ppsspp/issues/15698 "Osk: Allow upper/lower for all keyboards"
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
[#17406]: https://github.com/hrydgard/ppsspp/issues/17406 "Even more crash fixes"
[#17414]: https://github.com/hrydgard/ppsspp/issues/17414 "GPU: Remove JumpFast/CallFast"
[#17415]: https://github.com/hrydgard/ppsspp/issues/17415 "Misc fixes and checks"
[#17422]: https://github.com/hrydgard/ppsspp/issues/17422 "Windows: Fix initial window show for all displays"
[#17425]: https://github.com/hrydgard/ppsspp/issues/17425 "Android: Explicitly allow content URI intents"
[#17412]: https://github.com/hrydgard/ppsspp/issues/17412 "Controller mapping fixes"
[#17420]: https://github.com/hrydgard/ppsspp/issues/17420 "Fix glitch when mapping analog inputs, caused"
[#17401]: https://github.com/hrydgard/ppsspp/issues/17401 "Additional crash fixes"
[#17399]: https://github.com/hrydgard/ppsspp/issues/17399 "More crash fixes"
[#17398]: https://github.com/hrydgard/ppsspp/issues/17398 "OpenXR - Cleanup unsupported features, support Android 12"
[#17374]: https://github.com/hrydgard/ppsspp/issues/17374 "Assorted crash fixes and asserts"
[#17370]: https://github.com/hrydgard/ppsspp/issues/17370 "Fix ScrollView crash (though the root cause is a race condition most likely)"
[#17392]: https://github.com/hrydgard/ppsspp/issues/17392 "Misc crash fixes from mystery thread"
[#17394]: https://github.com/hrydgard/ppsspp/issues/17394 "Bump the index/vertex cpu-side buffer sizes a little."
[#17380]: https://github.com/hrydgard/ppsspp/issues/17380 "Pre-scan the root of texture packs for hash-named files."
[#17393]: https://github.com/hrydgard/ppsspp/issues/17393 "Prevent tilt-controlled left/right dpad butons from getting stuck"
[#17396]: https://github.com/hrydgard/ppsspp/issues/17396 "Vulkan: Turn off the ubershader on Mali T880, T860 and T830 on old driver versions"
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