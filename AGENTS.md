# PPSSPP Agent instructions

These rules apply to this repository by default.

Ignore the folder ai_instructions in the root directory, it's old stuff from contributors.

## General instructions

1. Keep style changes minimal unless requested. Follow existing code patterns and conventions.
2. Keep cross-platform parity in mind when changing shared code. See below for more multiplatform tips

## Core Safety Checks

1. For HLE, CPU, GPU, timing, threading, and memory changes, call out regression risks explicitly.
2. Consider savestate compatibility when changing serialized state.

## Build and Validation

To verify that things build on Linux/Mac, use ./b.sh --debug. For Windows, use the Visual Studio solution in the Windows subdirectory.

In addition to the pspautotests runner (test.py), there is a separate binary with C++ unit tests
in the /unittest subdirectory. After substantial changes (at the end of a chunk of work, not
necessarily after every edit), run these too:

- Windows: build the `UnitTest` project (unittest/UnitTests.vcxproj), then run `Windows/x64/Debug/UnitTest.exe all`
- Linux/Mac: configure with `-DUNITTEST=ON`, then run `build/PPSSPPUnitTest all`

This runs all tests in `availableTests` in unittest/UnitTest.cpp. You can run a single test by
passing its name instead of `all`; no arguments lists the available tests.

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
Qt/main.cpp
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
- Remember to add any new `.cpp`/`.c` file to **five** places: `Core/CMakeLists.txt`, `Core/Core.vcxproj`,
  `Core/Core.vcxproj.filters`, `android/jni/Android.mk`, and `libretro/Makefile.common`. New `.h` files only need the
  first three (`Android.mk`/`Makefile.common` are plain compiled-source lists so headers
  don't go in them). Only the CMakeLists.txt change can be verified from a Linux/Mac build - the rest can't be
  build-tested here, so double check them by hand against how an existing neighboring file (e.g. `sceVaudio.cpp`) is
  listed in each. Note: New files in the unittest project have to be updated in the unittest part in android/jni/Android.mk.

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

## Debugging and breakpoint considerations

It might be worth trying the interpreter - all types of breakpoints are the most reliable with this CPU backend.
The JITs are much, much faster and in theory also support breakpoints, but especially from websockets there seem
to be trouble.

## Quick rebuild on Linux

You don't need to do ./b.sh --debug to verify every single little change, instead use this shortcut:

```bash
cd build ; make -j32; cd ..
```
