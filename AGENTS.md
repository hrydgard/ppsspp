# PPSSPP Agent instructions

These rules apply to this repository by default.

Ignore the folder ai_instructions in the root directory, it's old stuff from contributors.

## Detailed guides

The rules below are the short version. These docs hold the detail, look them up when the task calls
for it:

| Doc | When you need it |
|---|---|
| [docs/building.md](docs/building.md) | Build commands for every target (VS/MSBuild, CMake, UWP, legacy Android NDK, libretro), unit tests, pspautotests |
| [docs/debugging.md](docs/debugging.md) | Driving the WebSocket debugger and PPSSPPHeadless from a script, breakpoint reliability per CPU backend, debugging a game that works on hardware |
| [docs/DebuggerThreading.md](docs/DebuggerThreading.md) | `Core_RunOnCPUThread` / `g_frameMutex` / shutdown-lock rules - required reading before touching debugger code |
| [docs/HLEModules.md](docs/HLEModules.md) | Adding an HLE module or function, and the seven build files a new source file goes in |
| [docs/translations.md](docs/translations.md) | Translating UI strings with Tools/langtool |
| [docs/pspautotests.md](docs/pspautotests.md) | Workflow for improving PPSSPP using pspautotests |
| [docs/frametest.md](docs/frametest.md) | Framedump rendering tests |
| [docs/WebSocketDebugger.md](docs/WebSocketDebugger.md) | WebSocket debugger protocol reference |

## General instructions

1. Keep style changes minimal unless requested. Follow existing code patterns and conventions.
2. Keep cross-platform parity in mind when changing shared code. See below for more multiplatform tips
3. Never `git push` (to any remote) without asking the user first. Committing locally is fine when asked; pushing requires explicit approval.
4. **Don't write code on `master`.** When asked to make a code change while on `master`, create an
   appropriately named branch first (`git checkout -b some-descriptive-name`) and do the work there.
   If you're already on a topic branch, just keep working on it.
5. **Most files in this repo are CRLF** - `.vcxproj`, `.vcxproj.filters`, `android/jni/Android.mk`,
   `libretro/Makefile.common`, `AGENTS.md`, and much of the source. If you patch one with a script, read *and*
   write with `newline=''`; reading with Python's default universal-newline translation and writing with
   `newline=''` silently converts the whole file, turning a two-line addition into a 5000-line diff. Check
   `git diff --stat` before committing - a whole-file rewrite is obvious there and invisible in the editor.
   Prefer the Edit tool, which does exact string replacement and can't do this.
6. **Don't feed Python to `bash -c` via a heredoc when the code contains backslashes.** The Git Bash / MinGW
   layer strips one level of backslash escaping on the way in, *even with a quoted delimiter* (`<<'PY'`), which
   normally suppresses all substitution. So the script Python receives is not the one you wrote:

   | You write in the heredoc | Python actually sees | Result |
   |---|---|---|
   | `"\r\n"` | `"\r\n"` | fine - survives, because you *want* Python to interpret it |
   | `"\\n"` (intending a literal `\n` in the output) | `"\n"` | **a real newline is written into the file** |
   | `'foo \\\r\n'` as a match anchor | `'foo \<CR><LF>'` | **anchor silently doesn't match**, reported as "anchor missing" |

   The tell for the first case is a compiler error like `C2001: newline in string literal`; the second case
   produces no error at all, just a patch that quietly did nothing. Both are invisible in the heredoc you wrote.

   The rule: **a heredoc is fine as long as every backslash in the script is one you want Python to interpret.
   The moment you need a literal backslash in the *output*, stop.** Then either:
   - use the Edit tool instead (exact string replacement, no shell in the path - the best option for the common
     case of "insert a few lines of C++ that contain `\n`"), or
   - write the script to a file with the Write tool and run `python thescript.py`, or
   - build the backslash as `chr(92)` so no literal backslash appears in the heredoc at all.

   Note this is about the *Python source*, not the data: reading and rewriting a CRLF file with `newline=''` and
   `\r\n` anchors works fine in a heredoc, and is the normal way to patch files here (see rule 5).

## Core Safety Checks

1. For HLE, CPU, GPU, timing, threading, and memory changes, call out regression risks explicitly.
2. Consider savestate compatibility when changing serialized state.

## Build and validation

- Linux/Mac: `./b.sh --debug` for a full configure+build; after that, just `cd build ; make -j32; cd ..`
  for a quick rebuild.
- Windows: the Visual Studio solution `Windows/PPSSPP.sln` - always build through it, even if a stray
  CMake-generated `build/` directory exists at the repo root. An agent can drive it with `MSBuild.exe`
  (located via `vswhere.exe`) instead of the GUI:

```powershell
$installPath = & "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath
$msbuild = "$installPath\MSBuild\Current\Bin\MSBuild.exe"
& $msbuild "Windows\PPSSPP.sln" /t:UnitTest /p:Configuration=Debug /p:Platform=x64 /m
```

- Kill leftover `PPSSPPHeadless.exe`/`PPSSPP*.exe` instances before building - one holding the exe makes
  the link fail with `LNK1168`, which looks like a build problem and isn't.
- **A stale binary lies consistently.** After a `git stash` cycle that touched a header, do a
  `/t:Rebuild`; when bisecting a behavioural change, confirm the binary actually changed before you
  believe the result.

UWP, the legacy Android NDK build and the libretro core have their own build systems - see
[docs/building.md](docs/building.md), which also covers the above in full.

## Testing

After a chunk of work (not after every edit), run both suites:

- C++ unit tests: build the `UnitTest` project and run `Windows/x64/Debug/UnitTest.exe all`
  (Linux/Mac: configure with `-DUNITTEST=ON`, run `build/PPSSPPUnitTest all`). Tests are listed in
  `availableTests` in `unittest/UnitTest.cpp`; pass names instead of `all` to run a subset.
- pspautotests (HLE coverage) - run them **exactly the way CI does**:

```bash
python test.py -g --graphics=software
```

  **The `-g` matters**: without it you also get `tests_next`, the expected-to-fail to-do list, and
  around a hundred failures that mean nothing is wrong. The only meaningful result is `0 tests failed`.
  (The debug-CRT "Detected memory leaks!" dump after the summary line is normal, not a failure.)

New unit tests are added to `availableTests`; large ones go in their own file in `unittest/`, listed in
both CMakeLists.txt and the Visual Studio project. See [docs/building.md](docs/building.md) for the
details and [docs/pspautotests.md](docs/pspautotests.md) for a workflow for improving PPSSPP with
pspautotest results.

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

## Framedump rendering tests (frametests)

There is a rendering test system that replays GE frame dumps (`.ppdmp`) through PPSSPPHeadless and compares
the output against reference images, driven by the `frametests.py` script and a JSON config per test set.
When changing rendering code, consider running these tests. See docs/frametest.md for full documentation.

Note: `headless/Compare.cpp` reads back framebuffers top-down; the flip to bottom-up is only applied when
writing BMPs (and when reading BMP references). `TranslateDebugBufferToCompare` also exists as a copy in
`libretro/LibretroGraphicsContext.cpp` - keep the two in sync.

## Adding HLE modules

HLE module implementations live in `Core/HLE/sce<ModuleName>.cpp` / `.h`, as a `const HLEFunction
<name>[]` table registered via `RegisterHLEModule()`. Two savestate-compatibility rules that break
things silently if ignored:

- **New modules are registered at the very end** of the registration function in `Core/HLE/HLETables.cpp`,
  never inserted alphabetically among the existing `Register_*()` calls.
- **New entries in an existing module's function table go at the very end of that array too** - a
  savestate captures the syscall opcode encoding the entry's array index, so shifting later entries
  makes old savestates call the wrong function.

Also: a new `.cpp`/`.c` file has to be added to **seven** build files (CMake, Core.vcxproj + filters,
the two UWP projects, `android/jni/Android.mk`, `libretro/Makefile.common`); headers to the first five.
Full details, the format-string legend and the UWP build command are in
[docs/HLEModules.md](docs/HLEModules.md).

## Translated UI strings (assets/lang)

**When implementing new UI, translations come last, in their own commit after everything else is
done.** Write the English strings, get the feature built and working, commit that - then stop and ask
the user to check the English wording before translating anything.

Don't hand-edit the ~47 language files, and don't run langtool's own AI commands either - do the
translating yourself and let `Tools/langtool` do the file surgery. The workflow is in
[docs/translations.md](docs/translations.md).

## Debugging

PPSSPP has a JSON/WebSocket debugger and automation API (read/write memory, breakpoints, stepping, GPU
state, input injection, log tailing), served at `/debugger` on the Remote ISO port and enabled with
`--debugger=PORT` on both the application and headless builds. `Tools/wsdbg/` is a CLI client for it.

- Protocol reference and event catalog: [docs/WebSocketDebugger.md](docs/WebSocketDebugger.md) - read it
  before changing the interface, and update it when adding commands.
- Driving it from a script, plus the many headless gotchas (`--timeout` is for the whole session, `-r`
  is ambiguous, exceptions don't reach the log, ...): [docs/debugging.md](docs/debugging.md).
- Breakpoints are most reliable on the interpreter (`-i`); memory breakpoints only work for constant
  addresses under the JITs, and register breakpoints never trip there. Table in
  [docs/debugging.md](docs/debugging.md).
- **Before touching debugger code that runs off the CPU thread, read
  [docs/DebuggerThreading.md](docs/DebuggerThreading.md)** - `Core_RunOnCPUThread()` for mutations,
  `g_frameMutex` for hot reads, and a lock order that has deadlocked for real when gotten backwards.

## Commit message style

Keep commit messages focused, not overly long (although sometimes it's motivated if a single commit
is super complex). Do not report things like 100/100 tests passed - that's a given, if tests break
you aren't supposed to make a commit.

Omit the session marker.

## Making pull requests

Only make pull requests from your branches if the user requests it.

Prefix your PR messages with this: "### Claude says". Also omit the session marker.

## Code style

4-wide tabs, not spaces.

Instead of:

`printf("%.*s", (int)part.size(), part.data());`

we have a macro:

`printf("%.*s", STR_VIEW(part));`

Style example:

```cpp
class MyClass {
public:
  MyClass(int memberVar) : memberVar_(memberVar) {}
  int MemberFunc() const {
    int localVar = 0;
  }

private:
  int memberVar_;
  int initializedMemberVar_ = 0;
}
```

But generally follow the surrounding style. Braces are preferred on the same line. Braces are always used even when they could be omitted due the inner part being just a single line.

