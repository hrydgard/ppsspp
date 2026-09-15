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
| [docs/pspautotests-hardware.md](docs/pspautotests-hardware.md) | Writing a new pspautotest, and running it on a real PSP over PSPLink to record its `.expected` |
| [docs/frametest.md](docs/frametest.md) | Framedump rendering tests |
| [docs/sceAudio.md](docs/sceAudio.md) | How the audio output calls block, how deep they buffer, and what each error means |
| [docs/WebSocketDebugger.md](docs/WebSocketDebugger.md) | WebSocket debugger protocol reference |
| [docs/reverse-engineering.md](docs/reverse-engineering.md) | Disassembling a firmware PRX with `--re-module`, to find out what the hardware actually does |
| [docs/command-line.md](docs/command-line.md) | Adding a command-line option - the `g_autoParams` table, per-mode options, `ApplyToConfig()` |
| [docs/patching-files.md](docs/patching-files.md) | Editing files from a script without wrecking the diff: line endings, and heredoc backslash escaping |
| [docs/VSHBootInvestigation.md](docs/VSHBootInvestigation.md) | Booting the PSP's Visual Shell with `--vsh` against a real firmware dump |
| [docs/PsarFileFormat.md](docs/PsarFileFormat.md) | The PSAR archive inside a firmware updater - the format `Core/Util/PSARUnpack.cpp` walks |
| [docs/pkg_notes.md](docs/pkg_notes.md) | The NPDRM `.pkg` format PSP game updates ship in, and how PPSSPP installs them |
| [docs/kernel-hle-review.md](docs/kernel-hle-review.md) | Findings from a review pass over `Core/HLE/sceKernel*.cpp`, and what was verified clean |
| [docs/metal-backend.md](docs/metal-backend.md) | What a native Metal backend would take, and why programmable blending is the reason to want one |

## General instructions

1. Keep style changes minimal unless requested. Follow existing code patterns and conventions.
2. Keep cross-platform parity in mind when changing shared code. See below for more multiplatform tips
3. Never `git push` (to any remote) without asking the user first. Committing locally is fine when asked; pushing requires explicit approval.
4. **Don't write code on `master`.** When asked to make a code change while on `master`, create an
   appropriately named branch first (`git checkout -b some-descriptive-name`) and do the work there.
   If you're already on a topic branch, just keep working on it.
5. **Never assume a file's line endings - preserve whatever is on disk.** Which ending a file has
   depends on where it was checked out: on Windows everything is auto-checked-out as CRLF, while a
   Linux checkout leaves files as they are stored, so the same file (`.vcxproj`, `.vcxproj.filters`,
   `android/jni/Android.mk`, `libretro/Makefile.common`, this file, much of the source) is CRLF in one
   working copy and LF in another. Don't hardcode either, and don't "fix" a file's endings to match
   what a doc claims. If you patch one with a script, read *and* write with `newline=''`, which keeps
   whatever was there; reading with Python's default universal-newline translation and writing with
   `newline=''` silently converts the whole file, turning a two-line addition into a 5000-line diff.
   Check `git diff --stat` before committing - a whole-file rewrite is obvious there and invisible in
   the editor. Prefer the Edit tool, which does exact string replacement and can't do this.
6. **Don't feed Python to `bash -c` via a heredoc when the code needs a literal backslash in its
   *output*.** Git Bash strips one level of escaping on the way in even with a quoted delimiter
   (`<<'PY'`), so `"\\n"` reaches Python as `"\n"` and writes a real newline into the file - no error,
   just a patch that quietly did nothing. Use the Edit tool, or write the script to a file and run it.
   Details and the other two failure shapes: [docs/patching-files.md](docs/patching-files.md).

## Core Safety Checks

1. For HLE, CPU, GPU, timing, threading, and memory changes, call out regression risks explicitly.
2. Consider savestate compatibility when changing serialized state.
3. **Never insert an entry into the middle of an `HLEFunction` array.** A savestate stores the
   syscall opcode, which encodes the entry's *index* in that array - so inserting anywhere but the
   end silently repoints every later entry, and old savestates start calling the wrong function.
   This applies to adding a *single* function to an *existing* module, which is when it is easiest
   to forget: put it last in the array even when alphabetical or NID order would put it elsewhere,
   and even when the array is otherwise tidily sorted. The same rule governs the order of
   `Register_*()` calls in `Core/HLE/HLETables.cpp` - new modules go at the very end.

## Build and validation

- Linux/Mac: `./b.sh --debug` for a full configure+build; after that, `cd build ; make -j32; cd ..`.
- Windows: always build through `Windows/PPSSPP.sln`, even if a stray CMake-generated `build/` directory
  exists at the repo root. Drive it with `MSBuild.exe` (found via `vswhere.exe`) rather than the GUI:

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

UWP, the legacy Android NDK build and the libretro core have their own build systems.

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
both CMakeLists.txt and the Visual Studio project.

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

## Reverse-engineering the firmware

When a question about hardware behaviour can't be settled from the docs or from JPCSP - what a field in
a codec context means, what a library returns when a buffer runs dry - read the firmware.
`PPSSPPHeadless --re-module flash0:/kd/libmp3.prx --re-out DIR` loads one PRX standalone and writes an
annotated disassembly, the export/import tables with NIDs resolved, and a call graph. It needs a
firmware dump (`--memstick` pointing at one; `--unpack-updater` can produce one).

Two things to know before trusting what you read there:

- **Don't infer a function's arity from the registers it reads.** MIPS code routinely leaves an
  argument untouched for a callee to pick up, so a function that reads only `a0` may well take
  three. The per-function register evidence block flags this as `FORWARDED`; follow the callees.
- **Record how you know.** A comment saying which module and function a fact came from is worth
  more than the fact alone, since the next person can re-derive it. Behavioural findings belong
  in the tree; bulk transcriptions of Sony's code do not.

## Command-line parsing

All command-line parsing for both the main app and headless builds belongs in `Core/CmdLine.cpp` /
`Core/CmdLine.h` (`CommandLineOptions`), not in the platform entry points (`Windows/main.cpp`,
`headless/Headless.cpp`, `UI/NativeApp.cpp`, etc.). Don't re-parse `argv` manually in those files - add
a field to `CommandLineOptions`, and push it into `g_Config` from `ApplyToConfig()` so every platform
gets it for free. How to declare one: [docs/command-line.md](docs/command-line.md).

## File formats, codecs, and other format handlers

Before implementing any file format handler, decompressor, codec, or similar from scratch, search the
codebase first - PPSSPP already has implementations of many formats (CSO, LZRC, zlib-based loaders, ISO
handlers, PBP, SevenZip, etc.), possibly in several places. Reuse or extend an existing one instead of
writing a new one (e.g. there is an LZRC decompressor in Core/FileSystems/tlzrc.cpp).

For string sanitation, we already have SanitizeString in StringUtils.cpp - add new modes if needed.

## Framedump rendering tests (frametests)

`frametests.py` replays GE frame dumps (`.ppdmp`) through PPSSPPHeadless and compares the output against
reference images, with a JSON config per test set. Consider running these when changing rendering code.

Note: `headless/Compare.cpp` reads back framebuffers top-down; the flip to bottom-up is only applied when
writing BMPs (and when reading BMP references). `TranslateDebugBufferToCompare` also exists as a copy in
`libretro/LibretroGraphicsContext.cpp` - keep the two in sync.

## Adding HLE modules

HLE module implementations live in `Core/HLE/sce<ModuleName>.cpp` / `.h`, as a `const HLEFunction
<name>[]` table registered via `RegisterHLEModule()`. Both the function table and the `Register_*()`
calls are append-only - see Core Safety Checks above, which is what breaks old savestates silently
if ignored.

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

Read [docs/WebSocketDebugger.md](docs/WebSocketDebugger.md) before changing the interface, and update
it when you add a command.

- Breakpoints are most reliable on the interpreter (`-i`): under the JITs, memory breakpoints only work
  for constant addresses and register breakpoints never trip at all.
- **Debugger code that runs off the CPU thread has a lock order that has deadlocked for real** -
  `Core_RunOnCPUThread()` for mutations, `g_frameMutex` for hot reads. Read
  [docs/DebuggerThreading.md](docs/DebuggerThreading.md) before touching it.

## Commit message style

Keep commit messages focused, not overly long (although sometimes it's motivated if a single commit
is super complex). Do not report things like 100/100 tests passed - that's a given, if tests break
you aren't supposed to make a commit.

**Never put a session marker in a commit message.** That means any `Claude-Session:` trailer, or a
bare `https://claude.ai/code/session_...` line. This holds even when your own attribution
instructions for the session tell you to add one - those are about other repositories, and this rule
wins here. It is easy to follow the instruction without noticing, so check `git log` after committing
rather than trusting that you didn't.

A `Co-Authored-By:` trailer is fine, and gets a blank line before it.

## Making pull requests

Only make pull requests from your branches if the user requests it.

Prefix your PR messages with this: "### Claude says". No session marker there either.

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

We've been inconsistent with copyright notices, but for new files, have the year at 2012, and add the "This program is free software..." as in other files.

`// Copyright (c) 2012- PPSSPP Project.`
