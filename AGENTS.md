# PPSSPP Agent instructions

These rules apply to this repository by default.

Ignore the folder ai_instructions in the root directory, it's old stuff from contributors.

## General instructions

1. Keep style changes minimal unless requested. Follow existing code patterns and conventions.
2. Keep cross-platform parity in mind when changing shared code. See below for more multiplatform tips
3. Never `git push` (to any remote) without asking the user first. Committing locally is fine when asked; pushing requires explicit approval.
4. **Most files in this repo are CRLF** - `.vcxproj`, `.vcxproj.filters`, `android/jni/Android.mk`,
   `libretro/Makefile.common`, `AGENTS.md`, and much of the source. If you patch one with a script, read *and*
   write with `newline=''`; reading with Python's default universal-newline translation and writing with
   `newline=''` silently converts the whole file, turning a two-line addition into a 5000-line diff. Check
   `git diff --stat` before committing - a whole-file rewrite is obvious there and invisible in the editor.
   Prefer the Edit tool, which does exact string replacement and can't do this.
5. **Don't feed Python to `bash -c` via a heredoc when the code contains backslashes.** The quoting mangles them,
   and an anchor string like `'...MemBlockInfo.cpp \\\r\n'` silently fails to match, so the patch reports
   "anchor missing" for reasons that aren't visible. Write the script to a file and run that instead, building
   separators with `chr(92)` if need be.

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

After a `git stash`/`git stash pop` that touches a **header**, do a `/t:Rebuild` rather than trusting
the incremental build. Stashing rewrites files with timestamps that can leave objects looking newer
than the header they were compiled against, so adding or removing a class member produces a binary
where translation units disagree on the object layout. That shows up as `UnitTest.exe` segfaulting
before it prints anything, for every test including ones unrelated to the change - which looks like
a catastrophic code bug and is not one. If a build starts crashing inexplicably right after a stash
cycle, rebuild before investigating anything else.

More generally, **when you are bisecting a behavioural change, confirm the binary actually changed
before you believe the result** - check the executable's mtime, or have the code you just added log
something you can grep for. A stale binary is indistinguishable from a real regression, and it lies
consistently, so a bisect on top of one produces a confident, entirely fictional answer. This cost
several hours once: a link that silently failed left a stale `PPSSPPHeadless.exe` in place, every
subsequent "revert this and retest" step reported the same failure, and a change was blamed that a
later clean rebuild proved innocent. The `LNK1168: cannot open ... for writing` case (a still-running
instance holding the exe, see the headless section below) is the most common way to get there, which
is why killing leftover processes before building is worth doing unconditionally rather than only
when a build complains.

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

For string sanitation, we already have SanitizeString in StringUtils.cpp - add new modes if needed.

## Translated UI strings (assets/lang)

**When implementing new UI, translations come last, in their own commit after everything else is
done.** Write the English strings, get the feature built and working, commit that - then stop and ask
the user to check the English wording before translating anything. The English string is what all ~47
languages get derived from, so rewording it afterwards means redoing the whole sweep.

One .ini file per language, keyed by section and key against `assets/lang/en_US.ini`. **Don't hand-edit
the ~47 files**, and don't run the AI commands in `Tools/langtool` either - you can read the call site,
which its fixed prompt can't. Do the translating yourself and let the tool do the file surgery. Run it
from `Tools/langtool`:

1. Work out what the string actually means before translating it: find where it's used in the C++,
   what any `%1`/`%d` placeholders get substituted with, how long it can be without breaking the
   layout, and how neighboring keys are already phrased in each language (that's your style guide).
2. Write a scratch file, one line per language, named after the ini file minus the extension, with
   the English string under `en_US`:
   ```ini
   [Single]
   en_US = Test string
   sv_SE = Teststräng
   lt-LT = Testeilutė
   ```
   No trailing `# comments` on those lines, they'd end up inside the translation. Placeholders have to
   survive verbatim. If you don't know a language well enough, leave it out - a key that's missing from
   a language file falls back to the English string at runtime, which is much better than a confident
   guess. If a language deliberately keeps the English string (a term like "Vsync" that isn't
   translated), do include it with the English text - it gets written with a `# same as English`
   comment, which stops langtool from trying to translate it again on every later run.
3. `cargo run -- import-single <scratch-file> <Section> "<Key>"` writes them all in, including a new
   key in en_US.ini, tagged `# AI translated` except for the en_US line, which is the string the rest
   were translated from. Note it overwrites existing values for that key, so take care with keys that
   already have human translations, and that the section has to exist already - langtool won't create
   one.
4. `cargo run -- validate` at the end, always. It checks that placeholders survived and exits
   non-zero if anything is off.

Optionally follow up with `cargo run -- copy-missing-lines` to give the languages you skipped the
English string as a placeholder. The other mechanical jobs (renaming and moving keys, sorting
sections) are langtool commands too - prefer them over editing the ini files by hand.

## Headless and unittest builds

We have additional PPSSPPHeadless and unit test builds (/headless and /unittest), that have their own separate
main functions (and also stub out most of the System_ functions as needed). Take these into account
when making cross platform changes.

New unit tests are added by listing them in availableTests in unittest.cpp. If they are large, put them in
separate files in the unittest subdirectory. Remember to update both CMakeLists.txt and the visual studio project.

A unit test is often the first thing to call a given function from outside its own .cpp, which makes the
`ppsspp_unittest` target in the legacy Android build (`android/jni/Android.mk`, see above) the strictest check
we have: MSVC links an `inline` function defined in a .cpp anyway, clang correctly does not. So a test can
build and pass on Windows and fail to link only on Android CI, with an undefined symbol pointing at a header
line. Fix it by dropping the bogus `inline` from the definition, not by avoiding the call.

pspautotests are a large set of tests of the PSP OS's API surface, and thus tests our HLE implementation.

**To check for regressions, run them exactly the way CI does** (see `.github/workflows/build.yml`):

```bash
python test.py -g --graphics=software
```

**The `-g` matters.** `test.py` keeps two lists: `tests_good` (the regression set - these pass and must keep
passing, ~314 of them) and `tests_next` (work-in-progress tests that are *expected* to fail, i.e. the to-do list).
`-g` runs only `tests_good`; with no flag you get `tests_next + tests_good` and around a hundred failures that mean
nothing is wrong. Don't go hunting those, and don't report them as regressions - the only meaningful result from
`-g` is `0 tests failed`. (`-b` runs only `tests_next`; `-m` prefix-filters whichever list is selected.)

Note the runner prints a debug-CRT "Detected memory leaks!" dump after the summary line on Windows debug builds.
That's normal and not a test failure - read the `N tests passed, N tests failed` line, which comes before it.

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
- **The same rule applies one level down: new entries in an *existing* module's `const HLEFunction <name>[]` table must
  go at the very end of that array too, never inserted alphabetically or next to a "related" existing entry.** A
  resolved import gets written directly into guest RAM as a syscall opcode that encodes the entry's array position
  (`modulenum<<18 | funcindex<<6`, see `GetSyscallOp()` in `Core/HLE/HLE.cpp`), and a savestate captures that opcode
  verbatim - inserting a new entry before an existing one shifts every later entry's index, so loading an older
  savestate after such a change resolves those later entries to the *wrong* function. Adding an all-new table (a new
  module, including a new alias-module `..._driver[]` variant of an existing one) is unaffected, since no old
  savestate could reference indices into a table that didn't exist yet - only *existing* tables need this care.
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

The `bundled` branch only holds built output, so `assets/debugger/static/js/main.*.js` in this tree is minified -
don't try to answer "does the web GUI use this event/parameter?" by reading it, the identifiers are mangled and you
will guess wrong. **The unminified source is at https://github.com/unknownbrackets/ppsspp-debugger** (default branch,
not `bundled`) - fetch or grep that when you need to know what the official client actually sends, e.g. before
changing or removing part of the protocol.

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

### Driving a headless debugger session (gotchas)

A working invocation, and the traps around it:

```bash
./Windows/x64/Debug/PPSSPPHeadless.exe -i --debugger=34567 --timeout=100000 --graphics=software --log \
    --root pspautotests/tests/../ pspautotests/tests/cpu/cpu_alu/cpu_alu.prx > hl.log 2>&1 &
# wait for "Listening on port" in hl.log, then:
./Tools/wsdbg/target/release/wsdbg.exe 34567 --sync --sync-timeout 15 < script.txt
```

- **`--timeout` is wall-clock seconds for the whole session**, not per test - the default is infinity, but as soon as
  you pass one it applies to your whole interactive debugging session too. Pass something huge (`--timeout=100000`);
  otherwise the process prints `TIMEOUT` and exits out from under you mid-session. (There's an escape hatch: the
  deadline check is skipped while `IsDebuggerPresent()`, i.e. under a native debugger.)
- **Prefer `--debugger=0` and scrape `Listening on port N` from that run's own log** over hardcoding a port. Also
  `taskkill //F //IM PPSSPPHeadless.exe` between runs for hygiene (Git Bash here has no `pkill`) - leftover
  instances are easy to accumulate when a script leaves the CPU stopped at a breakpoint.

  Some history, because it silently produced a round of bogus results before it was fixed: `Common/Net/HTTPServer.cpp`
  used to set `SO_REUSEADDR`, which on Winsock means "allow binding a port someone else is already listening on"
  (unlike POSIX, where it only covers TIME_WAIT). Two instances would *both* bind the same explicit port and *both*
  log `Entering web server loop. Listening on port 34567`, with the winner of any given connection undefined - so a
  client aimed at a fixed port could end up driving a leftover process running a different binary, CPU backend, or
  game. It's `SO_EXCLUSIVEADDRUSE` on Windows now, so the second bind fails honestly, and a non-zero `--debugger=PORT`
  that can't be honored is fatal in headless (exit 1) instead of falling back to a random port. If you still suspect
  you're talking to the wrong process, the `version` response carries `pid` and `path` - check them.
- **The headless build defaults to JIT** (`Headless.cpp`, `CPUCore cpuCore = CPUCore::JIT`), despite
  `g_Config.iCpuCore` being force-set to INTERPRETER just above - `ApplyToConfig()` has the final say. Pass `-i` for
  the interpreter.
- **`-r` is ambiguous in headless**: it's both "use IR interpreter" (legacy short cpu-core flag) and `--root`'s short
  form. Passing `-r` makes it eat the *next* argument as the root path, silently dropping e.g. `--debugger=PORT` so
  the server never starts. Use `--cpu=ir` instead. (`-i`, `-j`, `-J` are unambiguous.)
- When a test finishes, headless exits and the WebSocket connection closes (`CloseFrame { code: Away }`). So "the
  connection just closed" after a `cpu.resume` normally means **the breakpoint you were counting on never tripped**
  and the game ran to completion - not a transport problem.
- Some events deliberately never respond while the CPU is stepping, so `--sync` will burn its full timeout on them:
  `gpu.stats.get` and `gpu.stats.feed` (documented - they answer after the next flip), `gpu.record.dump`, and
  `input.buttons.press` (waits for N frames). Resume the CPU first, or skip them in scripted runs.
- Log broadcasts drown scripted output. Send this first:
  `{"event":"broadcast.config.set","disallowed":{"logger":true,"input":true}}`. Note `wsdbg`'s `key=value` shorthand
  can't build nested objects - paste raw JSON lines (any line starting with `{` is sent verbatim) for those.
- Keep wsdbg scripts in files and pipe them in, rather than building JSON inline in a shell command - inline
  `{"event":...}` in a bash heredoc trips Claude Code's command analyzer ("brace with quote character") and forces a
  manual approval prompt for every single invocation.
- **`--sync` can only match a response to a request that carries a ticket**, and wsdbg only assigns tickets to its
  `key=value` shorthand. A raw JSON line (needed for nested params) gets no ticket, so `--sync` just waits for the
  next message and treats whatever broadcast arrives first as the answer, silently desynchronising the rest of the
  script. Use the shorthand wherever the parameters are flat. Hex works there: `memory.disasm address=0x08804000`.
- **Headless reports `SYSPROP_HAS_DEBUGGER` as false** (only `Windows/main.cpp` implements it), so anything gated on
  it does nothing there - `LoadSymbolsIfSupported()` in `Core/System.cpp`, for instance, doesn't load `.ppmap`/`.sym`
  at all under headless. Gate new debugger-adjacent features on their own config flag, not on that property.
- Headless defaults its memstick to `memstick` next to the executable (`Headless.cpp`). Pass `--memstick=DIR` to
  point it at a real one instead - e.g. the same directory the app build uses - rather than copying a game in.
- Kill leftover instances (`taskkill //F //IM PPSSPPHeadless.exe`) before building - a running one makes the link
  step fail with `LNK1168: cannot open ... for writing`, which looks like a build problem and isn't.
- Don't wrap a script that starts headless in `timeout` - when it fires it takes the emulator down with it, and if
  the emulator was stopped at the crash you were investigating, that state is gone. Let the launcher exit and leave
  the process running; wsdbg can reconnect to the same port as many times as you like.
- Response field names are not uniform: `memory.read_u32` answers with `value`, while `cpu.getReg` answers with
  `uintValue`. A parser defaulting a missing key to 0 will quietly report zeroes - read the handler's comment in
  `Core/Debugger/WebSocket/*Subscriber.cpp` rather than guessing.
- `broadcast.config.set` accepts all five categories now (`logger`, `input`, `game`, `stepping`, `breakpoint`);
  it used to reject `game` and `stepping` until each had happened to fire once.
- **A script has to keep the connection open long enough for what it asked for to happen.** `cpu.runUntilTime`
  followed immediately by `:quit` disconnects before the run even starts, and it looks exactly like the feature
  not working. End with a `:wait cpu.stepping <seconds>`.
- **Exception and crash messages do not reach the log in headless.** It registers its own debug-output listener
  (`SendDebugOutput` in `headless/Headless.cpp`) that `fwrite`s to stdout, which is block-buffered when you
  redirect it to a file - so the output sits in the CRT buffer while the process runs, and `taskkill //F` throws
  it away rather than flushing. To actually read a crash trace, give that run a short `--timeout` and `wait` for
  the process to exit on its own.
- **`0xFFFFFFFF` is not an invalid instruction** - it decodes to `vflush`, a real Allegrex VFPU op, so writing it
  over code to test illegal-instruction handling just runs it. Check what an encoding actually is with
  `memory.disasm` before assuming it's garbage; the interpreter raises `ExecExceptionType::ILLEGAL` only when
  `MIPSGetInstruction` has no interpreter for it (`tge`/`tlt`/`teq` and friends).
- **To line input injection up with a wall-clock repro, use `cpu.status`'s `us` field** (emulated microseconds), not
  `ticks`. The PSP's clock frequency is changeable and games do change it - CrossCraft Classic runs at 333MHz, so
  `ticks / 222000000` is off by a factor of 1.5. `clockHz` is reported alongside.

## Debugging a game that works on hardware but not in PPSSPP

First, turn on `bAutoSaveLoadSymbols` (`--auto-save-load-symbols` in headless): when homebrew ships its
unstripped ELF next to the EBOOT (`app.elf` alongside `app.prx`, common for Zig/Rust/SDK homebrew), PPSSPP loads
the function and data names out of it, so the disassembly reads `world.init_empty` instead of `z_un_088c00f0`.
prxgen strips the symbol table on the way to the PRX, which is why the loaded module has none of its own.

The same flag also loads DWARF line info from that ELF (`Core/Debugger/LineInfo.h`), so addresses turn into
`mesh.zig:163` in backtraces, crash traces, breakpoint hits and log lines, both call stack views, and the
disassembly status bar. Availability is narrow and worth knowing before relying on it: **PRX conversion strips
every `.debug` section**, verified across all 437 pspautotests `.prx` and CrossCraft's own `app.prx`, and of 24
installed homebrew EBOOTs *none* carry debug info - CrossCraft only does because it ships `app.elf` separately.
So it's there for homebrew you're developing (or a plain `.elf` you built), never for a commercial game. DWARF 2
through 4 are decoded (psp-gcc emits 2, Zig 4); v5 re-encoded the file table and its units are skipped with a
warning rather than mis-parsed.

That same file is also a ground-truth oracle for anything the loader computes. It still has the symbol table (so
an address can be turned into a
function name) and the full `.rel.*` sections *with symbol indices*, which the PRX format throws away. That makes it
possible to check the emulator's work exhaustively offline - for the HI16/LO16 relocation bug, "does the address
this pairing produces land inside the section its symbol belongs to" turned a guess into a measurement over 8589
relocations, and immediately showed that the first fix attempt scored worse than the code it replaced.

Reach for that before trying to reason a fix out of a disassembly. A few dozen lines of Python over the ELF beats
re-running the game.

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

### Lock ordering: `g_frameMutex` before `Core_LockAgainstShutdown()`, always

`Core_LockAgainstShutdown()` / `CoreShutdownLock` (a `recursive_mutex`, `g_shutdownLock` in
`Core/Core.cpp`) is held across `CPU_Shutdown()` and `Memory::Reinit()`, i.e. while the core is
going away. Take it on any thread other than the CPU thread before reading core state - emulated
memory, the symbol map, kernel objects - so none of it is freed mid-read. It was called
`Memory::Lock()` and only covered the memory map; the name misled people into thinking it was about
memory access. When a function needs both it and `g_frameMutex`, **take `g_frameMutex` first**.

The CPU thread's order is structural and can't be changed: `NativeFrame()` wraps everything below it
in `g_frameMutex`, and several things under there lock memory - `Core_ProcessCPUQueue()` running a
queued WebSocket handler, and `runImDebugger()` -> `ImMemView` -> `DisassembleRange()`. So the
GUI-thread side is the one that has to match. (Getting it backwards deadlocked for real: a paint
handler held the memory lock and waited for `g_frameMutex` while the CPU thread did the reverse.)

Also: **a `Core_RunOnCPUThread()` callback does not need `Core_LockAgainstShutdown()`** - teardown only happens
on the CPU thread itself (`Memory::Shutdown()` via `CPU_Shutdown()` <- `PSP_Shutdown()`, all callers
on that thread; `Memory::Reinit()` from `Memory::DoState()` on savestate load). Don't add one.

**The general rule behind both of these: never make the CPU thread wait for a thread that is (or may
be) waiting on the CPU thread.** `Core_RunOnCPUThread()` blocks until the CPU thread drains the
queue, so anything the CPU thread might block on must not be held across such a call. The WebSocket
debugger's `lifecycleLock` hit exactly this - it was held across a whole event handler, and the CPU
thread took it in `Core_NotifyLifecycle(STOPPING)`, so stopping a game with a debugger request in
flight hung both threads. That lock is gone now; the rule is what's left of it.

**The WebSocket debugger no longer has any lock guarding it against startup/shutdown, and must not
grow one back.** The invariant instead is: a handler either does its emulator-state access inside
`Core_RunOnCPUThread()` - which serializes it against startup and teardown, since those run on the
CPU thread too - or touches only state that carries its own lock (the log ring buffer, `ctrlMutex`,
`GPUStepping`'s pause-action rendezvous). When adding a subscriber, put the core access in the
queued callback, including the `isAlive()`/`IsValidAddress()` checks: answering those outside it
just means acting on an answer that may already be stale.

`game.*` and `cpu.stepping`/`cpu.resume` are pushed rather than polled - `WebSocketDebuggerTick()`
(called from `Core_ProcessCPUQueue()`) notices the transition on the CPU thread, formats the event
there, and drops it in a per-connection mailbox. Don't add a broadcaster that reads emulator state
from the connection's own thread; produce the event on the CPU thread and push it instead.

The Win32 debugger's GUI-thread readers *do* still need it, so don't "simplify" those away:
teardown is not yet fully inside the `g_frameMutex` span. `EmuScreen::render()`'s `PSP_Shutdown()` is
inside it, but the ones in `EmuScreen::sendMessage()` (`REQUEST_GAME_RESET`, loading a new game) run
from `g_screenManager->sendMessage()` in `NativeFrame()`, which sits *above* where the guard is
taken. Closing that hole - moving those shutdowns inside the span, or deferring them to render time -
is the prerequisite for dropping the shutdown lock from the debugger entirely.

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
The JITs are much, much faster and in theory also support breakpoints, and we're trying to make the JITs
as reliable, but are maybe not quite there.

Concretely, as measured against the headless build (2026-08-16), per CPU backend:

| | interpreter (`-i`) | JIT (`-j`) / IR JIT (`-J`) |
|---|---|---|
| `cpu.breakpoint.*` (exec) | works | works |
| `cpu.stepInto/Over/Out`, `runUntil`, `nextHLE` | works | works |
| `memory.breakpoint.*` (memchecks) | works | **only for constant addresses** - see below |
| `cpu.regBreakpoint.*` | works | never trips (as documented) |

## Commit message style

Keep commit messages focused, not overly long (although sometimes it's motivated if a single commit
is super complex). Do not report things like 100/100 tests passed - that's a given, if tests break
you aren't supposed to make a commit.

## Quick rebuild on Linux

You don't need to do ./b.sh --debug to verify every single little change, instead use this shortcut:

```bash
cd build ; make -j32; cd ..
```
