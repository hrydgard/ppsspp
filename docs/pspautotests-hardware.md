# Writing pspautotests and running them on a real PSP

[docs/pspautotests.md](pspautotests.md) covers running the *existing* tests against PPSSPPHeadless.
This document covers the other half: connecting a real PSP over USB, running a test on it, and
recording its output as the `.expected` file that becomes the ground truth.

You need this whenever the answer to "what does the hardware actually do?" isn't already in an
`.expected` file - which is most of the time when you're implementing something new.

## What you need

- A PSP with custom firmware and a USB cable. (Verified against firmware 6.61 / PSPLink v3.0.)
- **PSPLink** installed on the PSP, in `PSP/GAME/psplink`, and *running* - launched from the game
  menu. This is not the same as connecting the PSP in USB mass-storage mode; if the PC sees
  "PSP Type A" you're in USB mode, and you want "PSP Type B".
- The **pspdev toolchain** on the PC, which supplies `psp-gcc`, `pspsh` and `usbhostfs_pc`.
  On macOS it lives in `~/pspdev` by default and is *not* on `PATH`, so prefix commands with
  `PATH="$HOME/pspdev/bin:$PATH"` or export it once per shell. On macOS you also need
  `brew install libusb-compat`; on Windows, the libusbK driver via Zadig.

Ask the user to connect the PSP and start PSPLink - you can't do it for them. To confirm it's
there before touching anything else:

```bash
ioreg -p IOUSB -l -w 0 | grep -i "USB Product Name"     # macOS; want "PSP Type B"
```

## The three moving parts

```
  PSP (PSPLink)  <--USB-->  usbhostfs_pc  <--TCP 3000-->  pspsh / gentest.py
```

- **`usbhostfs_pc -b 3000`** bridges USB to a TCP port and serves the PC filesystem to the PSP as
  `host0:/`. **Start it from the `pspautotests` root**, because `host0:/` is literally its working
  directory, and that's where a test's output files land.
- **`pspsh -p 3000`** is a shell on the PSP. `pspsh -p 3000 -e "<cmd>"` runs one command and exits.
  Handy ones: `ls`, `pwd`, `reset` (reboot PSPLink after a hung test), `pspver`, `power`, `scrshot`.
- **`gentest.py`** builds a test, runs it through `pspsh`, waits for it to finish and copies the
  output over the `.expected` file. It starts `usbhostfs_pc` itself if the port isn't already open.

Bringing it up, once, from the repo root:

```bash
export PATH="$HOME/pspdev/bin:$PATH"
cd pspautotests
(cd common && make)                 # builds libcommon.a; gentest.py refuses to run without it
usbhostfs_pc -b 3000 &              # leave running; prints "Connected to device"
pspsh -p 3000 -e ls                 # sanity check - should list the pspautotests directory
```

If `ls` shows `host0:/` contents you have a working chain. If it hangs or shows nothing, PSPLink
isn't running or the USB driver didn't bind.

## How a test reports its results

`common/common.c` wraps every test. Before `main()` it redirects the process's `stdout` and
`stderr` to `host0:/__testoutput.txt` and `host0:/__testerror.txt`, and after `main()` returns it
writes `host0:/__testfinish.txt` as a completion marker. So in the `pspautotests` root you'll see:

| File | Meaning |
|---|---|
| `__testoutput.txt` | the test's stdout - this is what becomes the `.expected` file |
| `__testerror.txt` | stderr; **non-empty means `gentest.py` refuses to write `.expected`** |
| `__testfinish.txt` | written on clean exit; its absence is how a timeout is detected |
| `__screenshot.bmp` | written by `emulatorEmitScreenshot()`; becomes `.expected.bmp` |

`gentest.py` deletes all four before each run, so a stale file can't be mistaken for a result.

That redirection is also why `usbhostfs_pc`'s working directory matters: get it wrong and the
output files appear somewhere unexpected, or the test can't open its data files.

## Writing a new test

A test is one directory under `pspautotests/tests/`, holding a `Makefile`, the source, the built
`.prx`, and the `.expected`. Minimal `Makefile` - use `common.mk`, not the older verbose style
still found in some directories:

```make
TARGETS = shortname

COMMON_DIR = ../../../common
include $(COMMON_DIR)/common.mk
```

`TARGETS` lists every test in that directory (one `.c`/`.cpp` per entry); `COMMON_DIR` is a
relative path, so count the levels. `common.mk` sets `BUILD_PRX = 1` and links `libcommon`.

The source includes `<common.h>`, which `#define`s `main` to `test_main` so the wrapper above runs.
Add `<sysmem-imports.h>` if you need `sceKernelSetCompiledSdkVersion*`. Then just `printf`.

```c
#include <common.h>
#include <pspiofilemgr.h>

int main(int argc, char **argv) {
	printf("result: %08x\n", sceIoSomething());
	return 0;
}
```

Two things `common.h` gives you that are easy to miss: `ARRAY_SIZE`, and `checkpoint()`, which
prefixes each line with `[r]`/`[x]` to record whether a reschedule happened - see
[docs/pspautotests.md](pspautotests.md) for what those markers mean. Use plain `printf` when
scheduling isn't what you're testing.

Then generate the expected output, from the `pspautotests` root:

```bash
python3 gentest.py io/shortname/shortname       # path under tests/, without .prx
```

It runs `make` in the test's directory first, then runs it on the PSP and writes
`tests/io/shortname/shortname.expected`.

Finally register the test in the repo root's `test.py`: new tests go in **`tests_next`**, and move
to `tests_good` only once PPSSPP passes them. Then check PPSSPP against the hardware:

```bash
python3 test.py --graphics=software io/shortname/shortname
```

### Design rules for a test that can actually pass

- **Only print things that are the same on hardware and in the emulator.** Kernel pointers, heap
  addresses and absolute times are not - PSPLink shifts the memory layout, so a test that prints
  them can never pass. Print offsets from a base, or ranges, instead.
- **Sort anything whose order isn't the point.** `sceIoDread` returns a real FAT directory in
  creation order and a host directory in whatever order the host filesystem gives; if you're
  testing names, `qsort` by name and the difference disappears.
- **Make it repeatable.** If the test creates files, delete them at the start *and* the end - an
  aborted run otherwise leaves state that changes the next run's output.
- **Choose an encoding with no ambiguity.** When dumping a buffer, don't render NUL as `.` if the
  data can contain a literal `.`. Pick a character the data can't contain (`|` is forbidden in FAT
  names, so it works there) - otherwise the `.expected` quietly lies.

## Gotchas

- **`gentest.py` runs `make` for the whole test directory, not just your test**, so a neighbour
  that doesn't compile stops your test before it ever reaches the PSP. Everything under `tests/`
  builds with pspdev GCC 15 as of the "Make the tests build with a current pspdev toolchain"
  commit; if you hit a broken one anyway, `gentest.py -k` (`--keep`) skips `make` entirely and
  you can build your own target by hand with `make yourtest.prx`.
- **Rebuilding a `.prx` is not free, so don't regenerate one you didn't change.** The binaries are
  committed and were built with a much older SDK. Rebuilding with the current toolchain grows them
  by roughly a third (`testgp.prx`: 117 KB to 191 KB), and it can change what a test *does*:
  `time_t` is 64-bit now, so `rtc/convert`'s `sceRtcSetTime_t(&pt, 62135596800ULL)` marshals
  differently than the committed binary and stops matching its own `.expected`. Check
  `git status` and revert any `.prx` you didn't mean to touch.
- **A `.prx` you did rebuild deserves a hardware run before you commit it.** Build it, run it, and
  diff the output against the committed `.expected` - if it differs, decide whether the test
  genuinely changed or whether the toolchain did. Note the committed `.expected` files have CRLF
  line endings (they were recorded on Windows) while a fresh run writes LF, so compare with
  `diff <(tr -d '\r' < __testoutput.txt) <(tr -d '\r' < the.expected)`.
- **Each test PRX stays resident after it runs.** Run a handful back to back and the next
  `Load/Start` fails with `0x80020190` (out of memory) - which looks exactly like a hung test.
  `pspsh -p 3000 -e reset` between runs, and wait for the PSP to come back before the next one.
- **`host0:` is not a FAT volume, and it isn't even the same across hosts.** It's PSPLink's bridge
  to the PC, so it inherits the PC's filesystem: `tests/io/directory` was recorded on Windows and
  does not match on macOS, where `..` reports a different size and short names come back as
  `1.txt` rather than `1.TXT`. Anything testing FAT semantics has to run against `ms0:` - create a
  scratch directory on the real memory stick and clean it up - and anything reading `host0:` will
  only reproduce on the OS it was recorded on.
- **A test that hangs leaves the PSP wedged.** `gentest.py` issues `pspsh -e reset` after a
  timeout, but if you ran the PRX by hand, do that yourself. Default timeout is 10s; raise it with
  `-t SECONDS`.
- **`tests_to_generate` in `gentest.py`** - the list used when you pass no arguments - is stale;
  several paths in it no longer exist. Always name the test you want.
- **`--sdkver` matters for some APIs.** `gentest.py --sdkver=6060010 --sdkver-func=606` makes the
  test call `sceKernelSetCompiledSdkVersion606()` at startup, and `-a`/`--all-versions` sweeps every
  known version reporting which ones behave differently - a fast way to find version-gated
  behavior. A test can also call `sceKernelSetCompiledSdkVersion*()` mid-run to cover several
  versions in one `.expected`.
- **The module must be named `TESTMODULE`** (`common.c` does this) or `gentest.py` prints the load
  line as an unexpected result.

## Kernel-mode tests

Some behaviour differs by privilege - `sceKernelCreateTlspl` accepts partitions 1, 3 and 4 from a
kernel module and rejects them with `ILLEGAL_PERM` from user mode - so occasionally a test has to
run as a kernel module. `tests/threads/tls/kernel` is the working example. Set `COMMON_KERNEL = 1`
in the Makefile before including `common.mk` and you get a kernel build:

```make
TARGETS = partition
EXTRA_OBJS = tlspl-imports.o
COMMON_KERNEL = 1

COMMON_DIR = ../../../../common
include $(COMMON_DIR)/common.mk
```

Three things make this work, and all three took a while to find, so don't undo them casually:

- **`USE_KERNEL_LIBS`, set automatically by `common.mk`.** The stock `crt0_prx.o` references
  `__libcglue_init`, which pulls in the whole of libcglue, which imports `sceNetInet`,
  `sceUtility` and the `ForUser` IO libraries. A kernel module that imports any of those fails to
  load with `8002013C` (library not found), and no amount of trimming `LIBS` helps because the
  reference comes from the startup object itself. `USE_KERNEL_LIBS` selects a different startup -
  and brings `-nostdlib`, so `common/kernelglue.c` supplies the newlib support hooks that go
  missing: `_sbrk` over a fixed heap, the `_read`/`_write`/`_close` stubs, no-op retargetable
  locks, and `module_start`.
- **Nothing may import a `ForUser` library.** `ThreadManForUser` is fine, but `SysMemUserForUser`
  is not, which is why `common.c` compiles the `sceKernelSetCompiledSdkVersion` helpers out of
  kernel builds - a function pointer table referencing them is enough to pull the stubs in.
  Watch for this: `psp-strings yourtest.prx | grep For` lists what you're importing. Beware also
  that `ThreadManForKernel` does not export the Tlspl calls - importing them from there links
  cleanly and then returns `8002013A`, library not yet linked, from every call.
- **Size.** The kernel partition has about 285 KB free with PSPLink resident, 242 KB of it
  contiguous, and loading over the cable needs room for the file *and* the loaded image at once.
  A kernel build therefore gets a 4 KB `schedfBuffer` and a 4 KB heap instead of the user build's
  64 KB and 21 MB. `psp-size yourtest.elf` plus the `.prx` file size against `meminfo`'s MAXFREE
  tells you whether it will fit; `800200D9` (memblock alloc failed) means it didn't.

Output works normally - `printf`, `checkpoint` and `schedf` all behave - but a kernel build writes
to `host0:` with `sceIo` directly rather than through newlib's `FILE`, since that's what dragged
libcglue in. Formatting still goes through newlib's `vsnprintf`, so the usual format specifiers
are all available.

**PPSSPP can't run these yet.** `PPSSPPHeadless` times out on a kernel PRX built this way, so a
kernel-mode test can't go in `test.py` - keep it as a hardware reference and diff it by hand
against its user-mode twin.

## Worked example: FAT short names

`tests/io/shortname` was written this way and is a decent template. It creates a scratch directory
on `ms0:`, fills it with names that exercise the 8.3 rules, and dumps the raw `d_private` block
that `sceIoDread` fills in.

It was written to *dump* rather than *decode* on purpose, and that immediately paid off: the
struct in the SDK's own `pspiofilemgr_dirent.h` does not match what firmware 6.61 writes when the
compiled SDK version is unset or below 3.08. The real layouts are

| Compiled SDK version | Layout |
|---|---|
| unset, or <= 3.07 | short name at byte 0 (13 bytes), long name at byte 13 - no size field |
| >= 3.08 | caller-supplied size at byte 0, short name at byte 4 (16 bytes), long name at byte 20 |

which is what `Core/HLE/sceIo.cpp` implements, now confirmed on hardware rather than inherited from
JPCSP. Decoding with the SDK struct instead would have produced strings truncated at the front and
an `.expected` that silently enshrined the mistake.

The test also caught three real emulator/hardware differences, still open in `tests_next`:
hardware preserves the original case in `d_name` (`readme.txt`) where PPSSPP uppercases it
(`README.TXT`), and hardware appends `~1` to the short name of any name that isn't already valid
uppercase 8.3 (`readme.txt` -> `README~1.TXT`, `MiXeD.txt` -> `MIXED~1.TXT`) where PPSSPP only does
so on a collision.

## Committing

`pspautotests` is a git submodule, so a new test is two commits:

1. Commit and push inside `pspautotests/` (the new test directory, including the built `.prx` and
   the generated `.expected`).
2. Commit in the PPSSPP repo, including **both** the `test.py` change and the bumped submodule
   pointer (`git add pspautotests`) - otherwise CI checks out the old submodule and the test
   doesn't exist.
