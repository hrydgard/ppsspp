# PPSSPP Agent instructions

These rules apply to this repository by default.

Ignore the folder ai_instructions in the root directory, it's old stuff from contributors.

## General instructions

1. Keep style changes minimal unless requested. Follow existing code patterns and conventions.
2. Keep cross-platform parity in mind when changing shared code. See below for more multiplatform tips
3. Never `git push` (to any remote) without asking the user first. Committing locally is fine when asked; pushing requires explicit approval.

## Core Safety Checks

1. For HLE, CPU, GPU, timing, threading, and memory changes, call out regression risks explicitly.
2. Consider savestate compatibility when changing serialized state.

## Build and Validation

To verify that things build on Linux/Mac, use ./b.sh --debug. For Windows, use the Visual Studio solution in the Windows subdirectory
(`Windows/PPSSPP.sln`) - always build through it, even if a stray CMake-generated `build/` directory exists at the repo root (e.g.
left over from WSL/MSYS2 experimentation); that directory is not the supported Windows build path and may not have a working
compiler toolchain wired up.

An agent can drive the VS solution non-interactively with `MSBuild.exe` instead of opening the `devenv` GUI. Locate it via
`vswhere.exe` (same tool/gotchas as described in the libretro section below) and build a specific project with `/t:`, e.g.:

```powershell
$installPath = & "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath
$msbuild = "$installPath\MSBuild\Current\Bin\MSBuild.exe"
& $msbuild "Windows\PPSSPP.sln" /t:UnitTest /p:Configuration=Debug /p:Platform=x64 /m
```

(swap `/t:UnitTest` for `/t:PPSSPPWindows` or another project name as needed; drop it entirely to build the whole solution).

In addition to the pspautotests runner (test.py), there is a separate binary with C++ unit tests
in the /unittest subdirectory. After substantial changes (at the end of a chunk of work, not
necessarily after every edit), run these too:

- Windows: build the `UnitTest` project (unittest/UnitTests.vcxproj), then run `Windows/x64/Debug/UnitTest.exe all`
- Linux/Mac: configure with `-DUNITTEST=ON`, then run `build/PPSSPPUnitTest all`

This runs all tests in `availableTests` in unittest/UnitTest.cpp. You can run one or more
specific tests by passing their names instead of `all` (space-separated, e.g. `UnitTest.exe
CmdLine Path Utf8`); no arguments lists the available tests.

Known environment-specific issue: in at least one sandboxed dev environment, the `Jit` test
(`unittest/JitHarness.cpp`) hangs indefinitely specifically during the `CPUCore::JIT_IR`
phase - confirmed unrelated to source changes (reproduces identically on unmodified checkouts)
and not a memory-access fault (`Memory::HandleFault` is never entered). Root cause wasn't
pinned down further (would need a native debugger attached to the hung process, not available
in that environment) but is very likely specific to that sandbox rather than a real PPSSPP
bug, since CI runs the equivalent of `UnitTest.exe all` on every commit across multiple
platforms without apparent issue. If `all`/`Jit` hangs in your environment, run every other
test by name instead (skip `Jit`) to still get real coverage.

## Multiplatform considerations

The emulator has multiple platform-specific entry points. Some of these will be merged or removed in the future, but are all
still there. To verify that a change works, technically we need to compile for all these systems, but in practice we'll
just compile locally and test the platform we are currently on, and let CI handle the cross platform considerations.

System_-prefixed wrapper functions implement kind of a platform wrapper for some functionality, and are implemented in
the following list of files for each system. If we change one, we need to change them all.

Windows/main.cpp
ios/main.cpp
SDL/SDLMain.cpp
UWP/PPSSPP_UWPMain.cpp
android/jni/app-android.cpp
libretro/libretro.cpp

## Legacy Android build (android/jni)

There is a legacy Android build using the raw NDK build system (`android/jni/Android.mk` + `ndk-build`), separate from the
gradle build in `android/`. It's hooked up on CI (see `.github/workflows/build.yml`, the `android` matrix entries) and is
useful for quick test builds (it can build `ppsspp_headless` and the unit tests for Android). You do not need to build it
by default, but if you want to test-build it locally:

- The NDK path is hardcoded in `android/ab.cmd` (Windows) or passed via the `NDK` env var to `android/ab.sh` (POSIX).
  It should match the `ndkVersion` in `android/build.gradle.kts`. The scripts copy assets first, then run ndk-build with
  a core count derived from the machine (nproc / %NUMBER_OF_PROCESSORS%).
- Example (POSIX): `cd android && NDK=/path/to/ndk ./ab.sh APP_ABI=arm64-v8a HEADLESS=1`
- The `ppsspp_headless` executable ends up in `android/libs/<abi>/`.

## libretro core build (Windows)

Canonical instructions are in `libretro/README_WINDOWS.txt` - read that first, this is a summary plus
agent-specific gotchas. The libretro core (`ppsspp_libretro.dll`) is built with a real `make`, not the
Visual Studio solution, even on Windows - it uses `cl.exe`/`link.exe` as the compiler/linker (via
`platform=windows_msvc2019_desktop_x64`), but orchestrated through GNU Make running inside an MSYS2
shell (a plain MSYS2 install, not "Git Bash" - typically at `C:\msys64`, needs `pacman -S make`).

```sh
cd libretro
make DEBUG=1 platform=windows_msvc2019_desktop_x64 -j32
```

(drop `DEBUG=1` for a release build; `-j` count doesn't need to match logical CPUs exactly). To test the
result, copy `ppsspp_libretro.*` into wherever the local RetroArch install reads cores from (e.g. its
`cores/` directory) and load it from within RetroArch.

An agent can drive this non-interactively by invoking `C:\msys64\usr\bin\bash.exe -lc "..."` directly
as a subprocess (the `-l` login-shell flag matters - it's what sets up MSYS2's own `PATH`, `make`,
`cygpath`, etc. correctly). In a sandboxed/agentic invocation (as opposed to a normal interactive MSYS2
terminal a human opens), two Windows environment variables the Makefile's VS-detection logic depends on
may not be inherited by the spawned process - `COMSPEC` (breaks the `cmd //c "bash VSWhere.sh ..."` call
used to locate Visual Studio) and `ProgramFiles(x86)` (which `VSWhere.sh` itself needs to find
`vswhere.exe`). If VS auto-detection fails this way, skip it by overriding `VsInstallRoot` directly on
the `make` command line (GNU Make command-line variables take precedence over the Makefile's own `:=`
assignment of the same name):

```sh
make VsInstallRoot="/c/Program Files/Microsoft Visual Studio/<year>/<edition>" DEBUG=1 platform=windows_msvc2019_desktop_x64 -j32
```

(path in MSYS2/cygpath POSIX form, not a raw Windows path; find the real value via `vswhere -latest
-property installationPath` if unsure of `<year>/<edition>`). This is a real full compile+link - prefer
it over trying to syntax-check libretro-specific files with a standalone `cl.exe /Zs` invocation, which
can miss real bugs (e.g. an include-order issue that leaves a platform macro like
`VK_USE_PLATFORM_WIN32_KHR` undefined before `vulkan.h`'s first, include-guarded inclusion, since a
narrower manual include-path/define set used for a syntax-only check may not reproduce the actual build
step's ordering).

## Command-line parsing

All command-line parsing for both the main app and headless builds belongs in `Core/CmdLine.cpp` /
`Core/CmdLine.h` (`CommandLineOptions`), not in the platform entry points (`Windows/main.cpp`, `headless/Headless.cpp`,
`UI/NativeApp.cpp`, etc.). Don't re-parse `argv` manually in those files - add a field to `CommandLineOptions` instead.

- Most options are declared in the `g_autoParams` table in `CmdLine.cpp` as `{offsetof(...), type, longName,
  shortName, docString, mode}`. `mode` gates the option to `CmdLineMode::Application`, `::Headless`, or `::Both`
  (the default if the field is omitted from the initializer) - the same long name can be reused for both modes with
  different types/meanings (e.g. `--log` is a `String` "log to FILE" option in Application mode but a `Bool` "full
  log output" option in Headless mode; they don't collide because a given `Parse()` call only matches params whose
  mode is `Both` or equal to the current mode).
- Options that can be repeated (e.g. `--ignore TESTNAME`, collected into a `std::vector<std::string>`) or that don't
  fit the generic single-value table need manual handling in the `else if` chain inside `CommandLineOptions::Parse()`,
  similar to how `--graphics=` and `boot Filenames` are handled.
- `ApplyToConfig()` is where parsed options get pushed into `g_Config`/`g_logManager`; prefer wiring a new option
  through there so all platforms get it for free, rather than reading `CommandLineOptions` fields ad-hoc at each
  call site.
- `NativeInit()` in `UI/NativeApp.cpp` still takes `argc`/`argv` (several platform entry points pass them in), but
  it shouldn't read them directly - by the time `NativeInit()` runs, `CommandLineOptions` should already have
  everything.

## File formats, codecs, and other format handlers

Before implementing any file format handler, decompressor, codec, or similar from scratch, search the
codebase first - PPSSPP already has implementations of many formats (CSO, LZRC, zlib-based loaders, ISO
handlers, PBP, SevenZip, etc.), possibly in several places. Reuse or extend an existing one instead of
writing a new one (e.g. there is an LZRC decompressor in Core/FileSystems/tlzrc.cpp).

## Headless and unittest builds

We have additional PPSSPPHeadless and unit test builds (/headless and /unittest), that have their own separate
main functions (and also stub out most of the System_ functions as needed). Take these into account
when making cross platform changes.

New unit tests are added by listing them in availableTests in unittest.cpp. If they are large, put them in
separate files in the unittest subdirectory. Remember to update both CMakeLists.txt and the visual studio project.

pspautotests are a large set of tests of the PSP OS's API surface, and thus tests our HLE implementation.

See docs/pspautotests.md for a workflow for running pspautotests and improving PPSSPP with the results.

## Framedump rendering tests (frametests)

There is a rendering test system that replays GE frame dumps (`.ppdmp`) through PPSSPPHeadless and compares
the output against reference images, driven by the `frametests.py` script and a JSON config per test set.
When changing rendering code, consider running these tests. See docs/frametest.md for full documentation.

Note: `headless/Compare.cpp` reads back framebuffers top-down; the flip to bottom-up is only applied when
writing BMPs (and when reading BMP references). `TranslateDebugBufferToCompare` also exists as a copy in
`libretro/LibretroGraphicsContext.cpp` - keep the two in sync.

## Adding HLE modules

HLE module implementations live in `Core/HLE/sce<ModuleName>.cpp` / `.h` (e.g. `sceOpenPSID.cpp`, `scePauth.cpp` are good
small examples to copy from). A module is a `const HLEFunction <name>[]` table of
`{nid, &WrapX_YYY<func>, "funcName", retChar, argString}` entries, registered via
`RegisterHLEModule("<name>", ARRAY_SIZE(table), table)` inside a `Register_<name>()` function declared in the header.

- `FunctionWrappers.h` has generic `WrapX_YYY<func>` templates for common signatures (return type X, args YYY) - add new
  wrappers there if you need a new signature.
- Format string legend for the `retmask`/argmask chars: `x` = u32 (shown as hex), `i` = int/s32, `f` = float, `X` = u64,
  `I` = s64, `v` = void.
- For functions of genuinely unknown purpose (only known by NID), name them `<moduleName>_<NID>` and stub them with
  `return hleLogError(Log::HLE, 0, "UNIMPL");` - an established pattern (see `scePauth.cpp`, `sceOpenPSID.cpp`).
- **New modules must be registered at the very end** of the registration function in `Core/HLE/HLETables.cpp` (look for
  the `// add new modules here.` comment near the end of that function) - not inserted alphabetically/logically among
  the existing `Register_*()` calls. Module registration order affects numeric IDs used in savestates, so inserting a
  new module earlier in that list would break save-state compatibility for saves made with older builds.
- Remember to add any new `.cpp`/`.c` file to **seven** places: `Core/CMakeLists.txt`, `Core/Core.vcxproj`,
  `Core/Core.vcxproj.filters`, `UWP/CoreUWP/CoreUWP.vcxproj`, `UWP/CoreUWP/CoreUWP.vcxproj.filters`,
  `android/jni/Android.mk`, and `libretro/Makefile.common`. New `.h` files need the first five (everything except
  `Android.mk`/`Makefile.common`, which are plain compiled-source lists so headers don't go in them). Double check
  each by hand against how an existing neighboring file (e.g. `sceVaudio.cpp`) is listed. Forgetting the UWP entries
  is easy to miss - the CMake and MSBuild (`Core.vcxproj`) builds both succeed silently, and it only surfaces as a
  UWP-only build failure (this has happened for real: `Core/MIPS/InterpreterDispatch.cpp` landed without its UWP
  entries, and the omission wasn't caught until someone actually built the UWP project). Note: New files in the
  unittest project have to be updated in the unittest part in android/jni/Android.mk.

  Both the CMakeLists.txt change (via a Linux/Mac build) and the `Core.vcxproj`/UWP changes (via MSBuild on Windows)
  can actually be build-tested, not just eyeballed - see "Build and Validation" above for the main Windows solution,
  and for UWP specifically:
  ```powershell
  $installPath = & "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath
  $msbuild = "$installPath\MSBuild\Current\Bin\MSBuild.exe"
  & $msbuild "UWP\PPSSPP_UWP.sln" /t:CoreUWP /p:Configuration=Debug /p:Platform=x64 /m
  ```
  (only `android/jni/Android.mk` and `libretro/Makefile.common` genuinely can't be build-tested here - see their
  respective sections above for what verification is possible for those.)

## WebSocket debugger

PPSSPP has a JSON/WebSocket debugger and automation API (connect, read/write memory, search memory for values or byte
patterns, set breakpoints, step the CPU, label data symbols, read GPU state, inject input, tail logs, etc.), served on
the same port as Remote ISO sharing at `/debugger` with subprotocol `debugger.ppsspp.org`. Implementation is in
`Core/Debugger/WebSocket.cpp` and `Core/Debugger/WebSocket/*Subscriber.cpp` (one file per feature area, each
documented at the top). Enable it via Settings > Tools > Developer Tools > "Allow remote debugger",
`RemoteDebuggerOnStartup` in the config, or `--debugger=PORT` on the command line (`0` = pick a port automatically) -
works on both the application and headless builds. On headless it also forces a break at start (`startBreak`), so the
CPU halts before anything runs. The bundled web GUI at `/debugger/` comes from the `assets/debugger` submodule
(`unknownbrackets/ppsspp-debugger`, `bundled` branch).

**Before touching this interface, read `docs/WebSocketDebugger.md`** - it has the full protocol reference and event
catalog (including which events are read-only vs. require `cpu.stepping` first). Don't guess event names or
parameters from memory; the doc (and each `*Subscriber.cpp` file's per-handler comments) is the source of truth, and
new events get added over time (e.g. `memory.search`, `hle.data.*`).

When adding new commands, don't forget to update `docs/WebSocketDebugger.md`,

To quickly get a live session going for manual testing (e.g. after adding/changing an event): build `PPSSPPWindows`
(see Build and Validation above), then run it with `--debugger=PORT` and something that keeps running/looping so the
CPU stays alive, so requests get a response instead of "CPU not started"/"CPU not active" errors. Any homebrew or
game works; PSP homebrew isn't checked into this repo, so if you don't already have something installed under
`memstick/PSP/GAME/`, ask the user for a `.iso`/`.cso`/`.elf`/`EBOOT.PBP` to boot, or to install one via the in-app
Homebrew Store. Watch the log output (`--log=somefile.log`) for the line `Listening on port N`, then point
`Tools/wsdbg/` at that port (`cargo run -- N <event> [key=value...]` for one-shot, or `cargo run -- N` for a REPL).
Most mutating events (`hle.func.*`, `hle.data.*`, memory writes while paused, etc.) require the CPU to be stopped
first - send `cpu.stepping` and `cpu.resume` to pause/unpause.

Alternatively use the headless build, Windows/{arch}/Debug/PPSSPPHeadless.exe or build/PPSSPPHeadless on CMake-based
platforms. Where arch is x64 or ARM64.

## Debugger threading model (Core_RunOnCPUThread / g_frameMutex)

CPU-thread-owned debugger state (`g_breakpoints`, `g_symbolMap`, `g_disassemblyManager`, registers,
memory, kernel threads) used to be touched directly from other threads (the WebSocket handler
thread, and the legacy Win32 debugger's message-pump thread) with no synchronization. Two
mechanisms now exist for doing this safely - pick based on whether you're mutating or just reading:

- **`Core_RunOnCPUThread(func)`** (`Core.h`/`Core.cpp`) - queues `func` to run on the CPU thread,
  blocking the caller until it's done. Use for *mutations* (breakpoint add/remove, register/memory
  writes, symbol map edits, stepping requests, thread wake/kill). Drained at the top of every
  `Core_RunLoopUntil()` iteration, so it's reached whether the CPU is running or stepping/paused;
  runs immediately if already called from the CPU thread. Two hard rules learned the hard way:
  never put a modal Win32 dialog call (`MessageBox`, `DialogBoxParam`, `InputBox_GetString`,
  `SomeDialog::exec()`) inside the queued lambda - it would block the CPU thread on user input, so
  split the function into "read/decide", "show modal", "mutate" pieces instead. And never call
  `SendMessage()` targeting one of your own GUI-thread windows from inside the lambda - the calling
  GUI thread is blocked waiting on the CPU thread rather than pumping messages, so a cross-thread
  `SendMessage()` back to it deadlocks; keep such calls outside the lambda instead.
- **`g_frameMutex`** (`Core.h`/`Core.cpp`) - a plain `std::mutex`, held by `NativeFrame()`
  (`UI/NativeApp.cpp`) only across the span where it actually touches that state
  (`g_breakpoints.Frame()` through `g_screenManager->render()` - where `Core_RunLoopUntil()`/actual
  CPU stepping happens - through `runImDebugger`/`renderImDebugger`), not across input handling or
  the present/frame-pacing waits. Use for *reads* invoked very frequently (`WM_PAINT`, a list
  reload triggered on every debugger-state-changed notification) where routing through
  `Core_RunOnCPUThread` would be too slow/heavy. The legacy Win32 debugger windows do this now -
  see `CtrlRegisterList::onPaint`, `CtrlDisAsmView::onPaint`, `CtrlMemView::onPaint`,
  `CtrlBreakpointList::reloadBreakpoints`, `CtrlThreadList::reloadThreads`, etc. in
  `Windows/Debugger/*.cpp`.

Architecture fact that makes `g_frameMutex` correct: regardless of graphics backend, `NativeFrame()`
(and thus CPU emulation via `Core_RunLoopUntil()`, and the Dear ImGui debugger) always runs on the
*same* thread - see `Core/EmuThread.cpp`. When a backend needs its own thread for actual graphics
API calls (`GraphicsContext::NeedsSeparateEmuThread()` - true for OpenGL, SDL, headless, libretro,
Qt; false for D3D11/Vulkan on Windows), the *original* thread stays behind purely to pump
`graphicsContext->ThreadFrame()` (i.e. just executes queued graphics API calls), and a *newly
spawned* thread takes over `NativeFrame()`/game logic/CPU duty. So `UI/ImDebugger/*.cpp` is always
safe to read/write this state directly, without either mechanism - it's always on the same thread
as `Core_RunLoopUntil()`. The legacy Win32 debugger is different: its dialogs are pumped by the
*original* `WinMain` message-loop thread, which is a genuinely separate OS thread from whichever
thread ends up running `NativeFrame()`/CPU, regardless of backend (this split happens one level
above the `NeedsSeparateEmuThread()` branch) - that's the whole reason it needed this mechanism.

**Update (2026-08-08): both `BreakpointManager`'s and `SymbolMap`'s internal mutexes have been
removed** after auditing every touchpoint across the codebase (WebSocket subscribers,
`Windows/Debugger/*.cpp`, `Windows/MainWindowMenu.cpp`/`Windows/MainWindow.cpp` main-window menu
items, `Core/Core.cpp`'s free-threaded `Core_Break`/`Core_Resume`, `UI/ImDebugger/*.cpp`,
JIT/interpreter backends, `Core/Debugger/MemBlockInfo.cpp`) and confirming each is covered by one of
the two mechanisms above or is already on the CPU/NativeFrame thread. Notably, `Windows/main.cpp`'s
`SortSymbols()` calls (fired from `System_Notify(BOOT_DONE)`/`System_Notify(SYMBOL_MAP_UPDATED)`)
turned out to already be safe without any change - both notifications are only ever fired from the
CPU/NativeFrame thread (`UI/EmuScreen.cpp`, `Core/HLE/sceKernelModule.cpp`), despite an old comment
there claiming reliance on the (now-removed) internal lock. `Qt/mainwindow.cpp`/`Qt/QtMain.cpp`
still poke at `g_symbolMap` directly and unguarded on the Qt UI thread - a pre-existing issue,
deliberately left alone since Qt isn't a maintained backend and is slated for removal; removing the
lock doesn't change `SymbolMap`'s public API, so Qt still builds, just without that safety net.
`GPU/Common/GPUDebugInterface.cpp` (GE debugger expression evaluation) and `Core/MemFault.cpp`
(crash-time diagnostics) also touch `g_symbolMap` and were deliberately not audited this round -
different subsystem / best-effort-by-nature respectively, follow up if they ever come up.

Two more things found while doing this:
- **`Core_RunOnCPUThread()`'s queue is only drained where `Core_RunLoopUntil()` runs, which requires
  a game to be loaded** (it's called from `EmuScreen::render()`). Calling `Core_RunOnCPUThread()`
  while at the main menu with nothing loaded used to hang forever. Fixed by also calling the
  (now public) `Core_ProcessCPUQueue()` directly from `NativeFrame()`, right before
  `g_screenManager->render()`, inside the same `g_frameMutex`-locked span - so it always runs, not
  just while a game is active.
- **Lock-ordering rule**: because `Core_ProcessCPUQueue()` is called from inside `NativeFrame()`'s
  `g_frameMutex`-locked span, any `Core_RunOnCPUThread()` lambda that itself tries to lock
  `g_frameMutex` (directly, or indirectly - e.g. by calling something like
  `CDisasm::NotifyMapLoaded()`, which locks it internally) will deadlock. Keep such calls outside
  the queued lambda, same as the modal-dialog and `SendMessage()` rules above.

Painting-problem design history, in case a similar tradeoff comes up elsewhere: routing every paint
through `Core_RunOnCPUThread` was rejected as too slow for something invoked continuously. A
per-window snapshot/cache with a per-row-rechecked `Core_IsStepping()` guard was tried first and
worked, but still had a narrow TOCTOU race (the CPU could resume between the check and that row's
reads) and the per-row-recheck pattern itself wasn't liked. Settled on `g_frameMutex` instead -
simpler, and actually race-free rather than just lower-risk. `CtrlRegisterList` shows live values
always now, grayed out by color alone (not cached) while the core is running, since a
constantly-moving value isn't meaningful to read closely anyway.

## Debugging and breakpoint considerations

It might be worth trying the interpreter - all types of breakpoints are the most reliable with this CPU backend.
The JITs are much, much faster and in theory also support breakpoints, but especially from websockets there seem
to be trouble.

## Quick rebuild on Linux

You don't need to do ./b.sh --debug to verify every single little change, instead use this shortcut:

```bash
cd build ; make -j32; cd ..
```
