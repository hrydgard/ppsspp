# pspautotests

This test suite is located in the `pspautotests` submodule/subdirectory at the root of the repo. Its `tests/` subdirectory contains tests arranged by category (audio/, cpu/, gpu/, threads/, etc.).

The runner script `test.py` at the repo root contains two lists: `tests_good` (known-passing regression tests) and `tests_next` (tests that don't yet pass — move things here → `tests_good` by fixing PPSSPP).

Tests are compiled as PRX binaries (a variation on ELF) which PPSSPP can load. They also have an `.expected` file alongside them, containing the reference output from running on a real PSP via PSPLink. The goal is for PPSSPPHeadless's output to match.

## Prerequisites

- **Initialize the submodule** (if not done): `git submodule update --init`
- The PRX binaries and `.expected` files are already committed inside the submodule — **no need to compile tests yourself** just to run them.
- To build PPSSPPHeadless itself, see the next section.

## Building PPSSPPHeadless

### Windows (Visual Studio)

Open `Windows/PPSSPP.sln` in Visual Studio and build the **PPSSPPHeadless** project, or use MSBuild directly:

```
MSBuild.exe -noAutoResponse Windows/PPSSPP.sln -target:PPSSPPHeadless -property:Configuration=Debug -property:Platform=x64 -maxCpuCount
```

The built executable will be at `Windows/x64/Debug/PPSSPPHeadless.exe`.

> CMake-based builds do exist but are **not the recommended way** on Windows — stick with the VS solution.

### Linux / macOS

Use `./b.sh --debug`, then find the executable under `build/` (e.g. `build/Debug/PPSSPPHeadless`).

## Running tests

### Using test.py (recommended)

Make sure Python is available. On Windows the Microsoft Store alias may interfere; use `py` or the full path to `python.exe`.

- Run all good tests: `py test.py -g`
- Run all broken/next tests: `py test.py -b`
- Run all tests (good + next): `py test.py`
- Run a specific test (or space-separated list): `py test.py -g cpu/cpu_alu/cpu_alu`
- Run a group of tests by prefix match (add `-m`): `py test.py -g -m audio/atrac`

### Direct headless invocation

```
Windows/x64/Debug/PPSSPPHeadless.exe --root pspautotests/tests/../ --compare --timeout=5 --graphics=software pspautotests/tests/audio/atrac/addstreamdata.prx
```

Instead of a single PRX, you can pass a directory (e.g. `pspautotests/tests/threads/mbx/...`) to run all tests under it, recursively.

**Key flags:**
- `--root` — points to the directory above `tests/` so the headless can find the expected directory layout.
- `--compare` — enables output comparison against `.expected` files.
- `--timeout=N` — seconds per test before killing it (default 5).
- `--graphics=software` — uses software GPU backend (required for headless; no real GPU available).

### What you'll see

**Passing test:**
```
pspautotests/tests/audio/atrac/addstreamdata.prx:
  audio/atrac/addstreamdata - passed!
1 tests passed, 0 tests failed, 0 tests missing.
```

**Failing test:**
```
pspautotests/tests/threads/mbx/refer/refer.prx:
O   hi 0 prio=00 next=OTHER  hi 1 prio=00 next=ITSELF  ...
E   hi 1 prio=00 next=ITSELF  ...
+   ...
0 tests passed, 1 tests failed, 0 tests missing.
Failed tests:
  threads/mbx/refer/refer
```

Lines prefixed with `O` are from the `.expected` file (real PSP), `E` is what PPSSPP produced, and `+` means a match.

The diff is line-by-line, so an `O` line followed by an `E` line at the same conceptual position means PPSSPP produced different output at that spot. A `+` line means both outputs agreed on that line.

## Workflow for fixing a test

1. Pick a test from `tests_next` in `test.py`.
2. Run it with headless to confirm failure and see what differs (`O` vs `E` lines).
3. Read the test source (`.c`/`.cpp`) and the `.expected` file to understand the API being tested.
   The `.expected` file was recorded from a real PSP — it's the ground truth. The test source
   reveals what syscalls are made and in what order. Sometimes the test deliberately corrupts
   state to probe kernel error handling.
4. Form a hypothesis: look for a systematic pattern in the diffs (wrong order, wrong error
   code, missing output). Cross-reference with multiple expected files that exercise the same
   API — they may reveal the PSP's real behavior from different angles.
5. Make changes to PPSSPP's HLE or other core code. Do not make super-targeted changes just
   to fix the test — instead, fix the underlying issue in a way that would also make sense
   on real PSP hardware.
6. Rebuild PPSSPPHeadless and re-run the test.
7. Run the full `tests_good` suite (`py test.py -g`) to check for regressions. A correct fix
   should not break any previously passing tests.
8. Rinse and repeat until it passes, then move it from `tests_next` to `tests_good` in `test.py`.

### Tips from experience

- The diff output compares the full text output line-by-line. To see PPSSPP's raw output
  without the diff overlay, omit `--compare`:
  ```
  Windows/x64/Debug/PPSSPPHeadless.exe --root pspautotests/tests/../ --timeout=5 --graphics=software path/to/test.prx
  ```
  There's another trick too, --print-equal-lines, which prints matching lines with a '=' prefix, so you can see the full output with context.
- Tests can show contradictory expected outputs at first glance. For example, the mbx/send
  test expected file shows FIFO message order (normal sends), while the mbx/refer test shows
  LIFO (after corruption) — because `sceKernelReferMbxStatus` on a real PSP *updates*
  `firstMessage` during traversal, changing the apparent head. Understanding the expected
  file's behavior often requires reading multiple related tests together.
- When searching for the underlying issue, trace through the HLE implementation with the
  test's syscall sequence. Check whether the kernel writes into PSP-visible memory — if so,
  test code can corrupt those values, and the PSP kernel may have specific handling for that.
- **Pointer addresses differ between PSP and PPSSPP.**  A test that prints raw kernel pointers
  (heap addresses, TLS block addresses, etc.) will always have mismatched expected output
  because PSPLink shifts memory layout.  Fix by changing the test to print offsets from a
  base address instead of absolute addresses.  After changing the test, run it on hardware
  to generate a new `.expected`.
- **The diff notation:**
  - `O` line = present in PPSSPP's output but not in expected (PPSSPP-only).
  - `E` line = present in expected but not in PPSSPP output (expected-only).
  - `+` line = context line (shown around diffs for context).
  - `=` line (with `--print-equal-lines`) = matching line.
  - `[r]` / `[x]` prefix = rescheduling occurred / did not occur since last line.
- **When a function is a stub** (`UNIMPL` in log), the expected output is a good specification
  for what to implement.  Look at multiple test cases in the expected file to understand the
  full range of valid and invalid inputs, return codes, and side effects.
- **Time-dependent tests** (RTC, timezone conversions) depend on the host machine's clock
  and timezone.  The PSP's `sceRtcParseDateTime` was a stub — the expected file showed
  exactly which RFC 3339 / RFC 2822 formats are accepted and which are rejected (return -1).

## Troubleshooting

- **"Python was not found" on Windows** — the Microsoft Store alias is interfering. Use `py` (the Python launcher) or the full path, e.g. `"C:/Users/.../AppData/Local/Programs/Python/Python314/python.exe" test.py -g`. Or disable the alias in Settings > Apps > Advanced app settings > App execution aliases.
- **No PRX files found** — run `git submodule update --init` from the repo root to fetch the `pspautotests` submodule.
- **PPSSPPHeadless exits immediately / "CPU not started"** — the test PRX may be missing or the `--root` path is wrong. Ensure `--root` points to the parent of `tests/`.
- **MSBuild error MSB1008** — the `MSBuild.rsp` response file may be interfering. Always pass `-noAutoResponse` on Windows builds.
