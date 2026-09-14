# Building and testing PPSSPP

Agent-oriented notes on the various build systems and test suites. This is the long version of the
"Build and validation" / "Testing" sections in [AGENTS.md](../AGENTS.md).

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
instance holding the exe, see [Debugging](debugging.md)) is the most common way to get there, which
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

## Quick rebuild on Linux

You don't need to do ./b.sh --debug to verify every single little change, instead use this shortcut:

```bash
cd build ; make -j32; cd ..
```
