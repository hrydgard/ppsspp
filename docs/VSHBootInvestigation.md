# VSH Boot Investigation

Also see jpcsp-vsh.md for how JPCSP does it.

Notes from an investigation into booting the PSP's Visual Shell (VSH / `vshmain.prx`) in
PPSSPP, using a real Sony firmware dump (not checked into the repo - gitignored, placed
locally under `assets/flash0/`). This is exploratory reverse-engineering of Sony's shipped,
undocumented System Software, not a supported PPSSPP feature - treat conclusions here as
best-effort, not verified against real hardware or any authoritative source.

Boot command used throughout: `--vsh --debugger=PORT [--break=log] [--appendconfig=FILE]`,
inspected live via `Tools/wsdbg` (see `docs/WebSocketDebugger.md`).

## Status summary

- **Headless vs. app build divergence** - root-caused and fixed (see below).
- **scePaf heap allocator crash** - root-caused, and a live-patch workaround was validated
  (the crash disappears entirely). Not yet fixed in a principled way in PPSSPP itself.
- **sceReg `/CONFIG/ALARM` polling loop** - investigated and found to *not* be a bug; it's
  a legitimate periodic check that only looks busy because nothing throttles emulation
  speed to real-time this early in boot.
- VSH does not yet reach a visibly-booted state (e.g. drawn XMB UI); the scePaf crash was
  the current blocker prior to the workaround, so what happens after it is unexplored.

## Issue 1: headless vs. app build behaved differently (RESOLVED)

`headless/Headless.cpp` hardcoded `g_Config.bIgnoreBadMemAccess = true` unconditionally,
overriding whatever the config said, with a comment noting three `pspautotests` tests
depend on this ("which is BAD"): `threads/mbx/refer/refer`, `threads/mbx/send/send`,
`threads/vtimers/interrupt`. The global default (`Core/Config.cpp`) is also `true`, but the
developer's own `memstick/PSP/SYSTEM/ppsspp.ini` had `IgnoreBadMemAccess = False` set
locally, so the app build (which loads the real ini) would hard-stop on any bad memory
access, while headless (hardcoded override) would silently log-and-ignore it and keep
running. This made the two builds diverge in how far VSH's boot got.

**Fix applied:** `headless/Headless.cpp` now defaults `bIgnoreBadMemAccess` to `false`,
matching the general/app default. This surfaces the scePaf crash (below) on headless too,
consistently with the app build. Rebuilt and verified: headless now hits the identical
crash (`Bad memory access detected! 0000000c ... Stopping emulation`) that the app build
does.

**Regression risk:** the three tests noted above were relying on bad-access-ignoring to
pass; they will likely now fail/crash on headless, presumably exposing whatever real bug
they were masking. This is presumably desirable (surfacing real bugs beats hiding them),
but worth knowing before this hits CI.

## Issue 2: scePaf heap allocator crash

### What scePaf is

`scePaf` ("Page Application Framework") is a GUI/windowing/memory-management middleware
Sony's System Software (VSH, Settings, and other 1st-party PSP apps) started using around
firmware 3.90+, replacing older common-dialog utilities. It is not officially documented;
what's known is from community reverse-engineering. Treat this as background context, not
verified fact.

### The crash

Deterministic, 100% reproducible, happens on both headless and app builds (given matching
`IgnoreBadMemAccess`), very early - essentially the first real work `vsh_module`'s startup
code does after `sceKernelCreateLwMutex`/`sceKernelMemset` calls into `scePaf`'s custom heap
allocator:

```
E MemMap: Write Word: Invalid access at 0000000c (size 00000000)  PC 0899bcc4 [scePaf_Module.text+13afc4] LR 0899bd38
```

Backtrace (innermost first), all addresses are runtime/relocated:

```
z_un_0899ba7c [scePaf_Module.text+13afc4]  (entry 0899ba7c, pc 0899bcc4)   <- crash here
z_un_0899bd88 [scePaf_Module.text+13b6e8]  (entry 0899bd88, pc 0899c3e8)
z_un_0899cdb8 [scePaf_Module.text+13c118]  (entry 0899cdb8, pc 0899ce18)
z_un_0899b4f4 [scePaf_Module.text+13a818]  (entry 0899b4f4, pc 0899b518)
z_un_0899b52c [scePaf_Module.text+13a878]  (entry 0899b52c, pc 0899b578)
vsh_module.text+3ec7c  (08842c64, pc 08842c7c)
vsh_module.text+6e00   (0880ad64, pc 0880ae00)
vsh_module.text+39ff8  (0883dfe4, pc 0883dff8)
vsh_module.text+944    (0880490c, pc 08804944)
vsh_module.text+18     (08804000, pc 08804018)     <- vsh_module entry point
vsh_module.text+f610   (088135f0, pc 08813610)     <- module_start (088135f0)
```

`scePaf_Module` (the PRX providing `scePaf`) loads at `0x08860d00-0x08a33500`. Its second
ELF segment (`Vaddr 0x18c540`, `Filesz 0x1078`, `Memsz 0x462a8`, loaded at `0x089ed240`) has
a large BSS tail (`Memsz` far exceeds `Filesz`) - relevant below.

### The call chain (all functions unlabeled/reverse-engineered live, no symbols available)

- **`z_un_0899ba7c`** (crash site) - inserts a free block into the heap's free-list. Reads a
  heap-struct field at `+0x350` and combines it with its size argument to compute a "top of
  block" value; separately loads a small value (`8` in the crashing run) off its own stack
  and writes through it directly (`sw v0, 4(v1)`, `v1=8` &rarr; guest address `0xC`). The `8`
  originates from an internal reservation helper's return value (below) treated as an
  absolute address when it's actually a relative offset.
- **`z_un_0899b9d0`** - a two-phase "commit a reservation" helper using two globals:
  `0x089F5430` (running watermark/counter) and `0x089F542C` (a "pending size" flag). Called
  with `a0=0`: pure query, returns current watermark. Called with `a0=size`: if
  `[0x089F542C] == size` (i.e. a matching reservation was pre-armed), it bumps the
  watermark by `size`, clears the pending flag, and returns the **old** watermark value as
  the reservation's starting offset (a pure bump-allocator; returns offsets, not pointers).
- **`z_un_0899ba18`** - heap-struct "reset to defaults": initializes 94 free-list bucket
  head nodes (at `heap+0x3C`, 8 bytes apart), and sets fixed fields: `+0x4=0x50` (flags),
  `+0x364=0x400`, `+0x368=1`, `+0x34C=0x40000`, `+0x354=0x40000`, `+0x34=heap+0x3C` (bucket
  array pointer, a real address), and explicitly **`+0x350 = 0`** - confirmed to be
  intentional default-init, not an omission.
- **`z_un_0899c610`** / **`z_un_0899c754`** - `free()`-like block coalescing logic. Treats
  `a0==0` as "free(null), no-op".
- **`z_un_0899cdb8`** - heap bootstrap wrapper. Calls `z_un_0899b774` to obtain a heap-struct
  slot, immediately stores its address into the global heap pointer `0x089F5428` (via a
  `jal` delay-slot store - see correction below), then calls `z_un_0899bd88` (the main
  allocator, in effect acting as "create the arena") and passes its result into
  `z_un_0899c610` ("free" - i.e. seed the free list with the whole arena as one block).
- **`z_un_0899b774`** - a **fixed static pool** of 16 heap-struct slots, 0x380 (896) bytes
  apart, starting at `0x089f5434` (so slots at `0x089f5434`, `0x089f57b4`, `0x089f5b34`,
  ...). Scans for a free slot (a "used" flag byte at each slot's start), `memset`s it to
  zero, marks it used, arms the reservation protocol (`[0x089F5430] = extra`,
  `[0x089F542C] = size`), and returns the slot address. This is the actual source of the
  `sceKernelMemset(089f5434, 0, 0x380)` seen in logs right before the crash.
- **`z_un_0899b4f4`** - calls `z_un_088937c0` first to fetch two values ("extra", "size"),
  then calls `z_un_0899cdb8` with them.
- **`z_un_088937c0`** - trivial getter, no computation: copies two fixed globals into its
  caller's out-parameters:
  - `0x089ED420` ("size") = `0x00850000` (~8.3MB) - falls in the **file-backed** part of
    scePaf's segment, i.e. a real, compiled-in constant. Not suspicious.
  - `0x089EE428` ("extra") = `0` - falls in the **BSS-zeroed** part of the segment (past
    the file-backed `Filesz` boundary). This is the field of interest.

### Root cause (best current understanding)

`0x089EE428` acts as the **base address** for the entire offset-based reservation
protocol (`z_un_0899b9d0`'s two globals). It stays `0` throughout the whole boot sequence -
confirmed by direct memory reads at multiple points, including immediately before the
crash. Nothing in the traced call chain ever writes a real value into it. Because it's
`0`, every "offset" the reservation protocol hands out (`0`, then `8` after alignment
padding) gets used downstream as if it *were* an absolute guest pointer, producing the
write to address `0xC`.

Working theory: real hardware's module loader (or an earlier system-init step) patches a
real memory-pool base address into this BSS location before scePaf's heap code ever runs.

**Checked and ruled out:** `0x089EE428` is *not* one of scePaf's own exported data
variables. Read the guest-side export tables directly (per the `PspLibEntEntry` layout in
`Core/HLE/sceKernelModule.cpp`: `resident` points to a NID array immediately followed by a
parallel address array) for both of scePaf's export ents - `scePaf_Module` itself
(`fcount=2, vcount=3, resident=0x089bcd84`) and the `scePaf` library entity
(`fcount=1078, vcount=24, resident=0x089bcdb4`) - and compared all 27 resulting
`(nid, addr)` pairs against `0x089EE428`. No match:

```
scePaf_Module vars:  f01d73a7@089bcc0c  11b97506@089c5600  0f7c276c@089c5604

scePaf vars:  074f12bf@089eca58  0aa88e9f@089edf78  1151ecc4@089c701c
              1e6bcc79@089ebd60  27b7331e@089eeff4  2fec12d8@089c68c0
              3e9224d3@089c6cbc  421a7bdd@089eb9d8  5f14b3d1@089eef7c
              932ad195@089eae78  95fee533@089ee7c8  b4b2d71e@089e7ec0
              b52933d3@089f5400  b6d4f51a@089ee7ec  bd24a083@089ee80c
              c41b6ac0@089edf68  d1201d51@089ea8f8  d8c6d943@089ee47c
              dacb88bf@089f1340  e04e5f6f@089ed424  e81b01ce@089eafc0
              e9fde3c4@089f5410  f44358f0@089f1360  feddaf5b@089ea810
```

Closest hits: `e04e5f6f@089ed424` sits just 4 bytes after the "size" constant
(`0x089ed420`), and a couple (`b52933d3`, `e9fde3c4`) sit near the heap-struct-pool start
(`0x089f5434`) - but nothing points at `0x089EE428` itself.

This rules out "some other module imports this var by NID and writes a real value into it"
as the mechanism - that path only ever touches addresses scePaf explicitly declares as
exported vars, and this isn't one of them. So whatever sets it on real hardware is either
(a) purely internal state that `scePaf_Module`'s own code sets up itself, via a self-init
routine somewhere in its ~1.8MB that we haven't traced/executed, or (b) something the
kernel patches directly through a non-NID mechanism (e.g. a fixed "system parameter block"
written during boot before any `module_start` runs), which PPSSPP doesn't replicate. This
wasn't pinned down further - see Open Questions.

**A note on a wrong turn during this investigation:** a `jal target`'s delay-slot
instruction executes with **pre-call** register state (before the callee runs), not
post-return state. An early pass through this trace mis-attributed a global-pointer store
as "the callee's return value" when it was actually the delay slot capturing a value set
*before* the call. Worth remembering next time - it's an easy trap when reading MIPS
disassembly linearly.

### Live-patch validation

With the emulator paused at `vsh_module`'s very first instruction (forced via
`AutoRun=False` in an `--appendconfig` ini, giving `coreParameter.startBreak=true`) - i.e.
before any scePaf code has run, but after all VSH-related modules are already loaded into
RAM - a plain `memory.write_u32` was used to patch a plausible pointer into the BSS slot:

```
memory.write_u32 address=0x089EE428 value=0x09000000
```

(`0x09000000` was hand-picked as a safely-free address: well below the top-of-memory
per-module stacks around `0x0bffc000+`, and well above the loaded modules which all sit in
`0x08800000-0x08a5xxxx`; no real allocation was performed, this was a debugger-only poke
for the live experiment, not the recommended permanent fix.)

Result: **the crash is completely gone.** Resuming after the patch, execution proceeds
normally past the point that always crashed before, into the (separate, non-buggy) sceReg
`/CONFIG/ALARM` polling loop, running indefinitely with ticks actively advancing and the
game state healthy (`VSHM01150` still loaded, no `CORE_RUNTIME_ERROR`). This strongly
confirms the root-cause theory above.

### Recommended permanent fix (not yet implemented)

Per the user's suggestion: reserve a real block via the user memory allocator (equivalent
of `sceKernelAllocPartitionMemory`, sized to at least the `0x089ED420` constant, ~0x850000
bytes) at the point `scePaf_Module` is loaded/started, and patch its address into
`0x089EE428` (relative to wherever `scePaf_Module` actually loads - it's PC-relative in the
sense that the `lui`/offset pair gets relocated at load time, so the *target* address to
patch must be computed the same way the relocated `lui v0,0x89F; lw ...,-0x1BD8(v0)` pair
resolves it, not hardcoded to `0x089EE428` verbatim if scePaf ever loads elsewhere).
Natural place to hook this: `Core/HLE/sceKernelModule.cpp`'s module-start/loading path,
special-cased for `scePaf_Module`, similar in spirit to how real hardware's loader
apparently does it.

## Issue 3: sceReg `/CONFIG/ALARM` loop (not a bug)

Earlier in this investigation this looked like an infinite busy-spin stuck on `alarm_0`.
Live backtrace + full disassembly of all 5 stack frames showed it's actually a bounded,
correct pass:

- An outer function (entry `0x0883f038`) iterates 2 categories (`s6=0,1`), each with a
  count from a small fixed table, and for each item calls a per-alarm check function
  (`0x0883e73c`) which itself calls the real `sceReg*` APIs.
- That check correctly finds nothing due (our registry data reports `time=-1`,
  `property=0` for every alarm slot - see `Core/HLE/sceReg.cpp`'s `tree_CONFIG_ALARM`).
- The **caller's caller** (entry `0x08818d14`, plausibly the module's main thread loop)
  does a real, correct wait: it calls `sceDisplayGetVcount`, computes an elapsed-vcount
  delta against a target interval, and calls `sceKernelDelayThread(0x2095)` (~8.3ms) in a
  loop until enough vcounts have passed, before repeating the whole check.

This is normal periodic-polling behavior. It only *looked* like a bug because nothing
throttles emulated time to real wall-clock speed this early in boot (no vsync to pace
against yet), so the ~8.3ms-paced loop runs at roughly 1000/sec instead of the intended
~120/sec. Not a blocker; no action needed here.

## Tools and methodology

All live inspection was done via the WebSocket debugger (`docs/WebSocketDebugger.md`) and
the `Tools/wsdbg` CLI client, primarily:

- `game.reset break=true` / `cpu.stepping` - force a pause at a known point (module entry,
  or immediately).
- `cpu.breakpoint.add address=... enabled=true` - **must** pass `enabled=true` explicitly;
  breakpoints default to `BREAK_ACTION_IGNORE` and won't actually pause otherwise.
- `cpu.getAllRegs` / `cpu.getReg` - register state at a breakpoint.
- `memory.disasm address=... count=...` - live MIPS disassembly with data-access
  annotations (very useful: shows the actual runtime address/value an instruction touched).
- `memory.read_u32` / `memory.write_u32` - inspect/patch specific globals.
- `memory.breakpoint.add ... write=true` - a write watchpoint. **Caveat found here:** this
  measurably perturbs JIT execution timing (likely forces checked/slow-path memory access
  broadly), and was observed to trigger a *different*, unrelated crash elsewhere in
  `vsh_module` before the watched address was ever touched. Treat results gathered while a
  memory breakpoint is active with suspicion; prefer plain reads at controlled breakpoints
  instead when reproducibility matters.
- `hle.backtrace` - stack backtrace while stepping, showing each frame's function entry,
  current pc within it, and stack pointer.

`--appendconfig=FILE` with `[General]` `IgnoreBadMemAccess = True` / `AutoRun = False` was
used to get a controllable, breakable app-build run without touching the real
`ppsspp.ini`. Only the app build (`UI/NativeApp.cpp`) supports `--appendconfig`; headless
does not.

## Open questions / next steps

- Find the *exact* mechanism real hardware uses to populate `0x089EE428`. Confirmed it's
  *not* one of scePaf's 27 exported data vars (checked all of them - see above), so it's
  either a self-init routine within `scePaf_Module`'s own code, or a non-NID kernel patch
  step. Searching all of `scePaf_Module`'s ~1.8MB of code for a write to that address (by
  pattern, not by hand) would help confirm there's truly no such write path in this
  firmware version, versus one we haven't found yet. It might be worth disassembling reboot.bin
  which is the real bootloader.
- Implement the permanent fix (real allocation + patch, done properly relative to
  wherever `scePaf_Module` loads) rather than relying on a debugger-only poke.
- Once past this crash, continue tracing VSH's boot to find the next blocker (if any)
  toward a visibly-rendered UI.
- Re-run the three `pspautotests` that relied on `IgnoreBadMemAccess=true` on headless, now
  that the default changed, to see what they actually surface.
