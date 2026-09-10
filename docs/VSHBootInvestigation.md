# VSH Boot Investigation

Notes on booting the PSP's Visual Shell (VSH / `vshmain.prx`) in PPSSPP via the direct HLE boot
path (`--vsh`), using a real Sony firmware dump (not checked into the repo - placed locally under
`assets/flash0/`, with `assets/flash1/` alongside it if you have one). This is exploratory
reverse-engineering of Sony's shipped, undocumented System Software, not a supported PPSSPP
feature - treat conclusions here as best-effort, not verified against hardware.

## Start here

One command boots VSH, runs to a fixed point, and tells you where it got. It starts the emulator,
finds the debugger port, connects, and cleans up after itself - no wrapper script needed:

```bash
printf 'cpu.runUntilTime us=6000000\ncpu.status\n:quit\n' | \
  Tools/wsdbg/target/release/wsdbg.exe --sync --compact --quiet \
    --launch Windows/x64/Debug/PPSSPPHeadless.exe --vsh -i --graphics=vulkan --loglevel=3 2>emu.log
```

That takes a few seconds. Protocol replies come back on stdout, the emulator's log on stderr.
Put more commands in the script to inspect state once it stops. Three things to know:

1. **Don't add `:wait cpu.stepping` after `cpu.runUntilTime`** (or any resume/step command).
   `--sync` already waits for it; a second one never arrives and burns the whole timeout. This is
   the single most likely reason a run seems to take minutes - emulation itself is ~real time.
2. **`-i` (interpreter)** is what this path is known to work with, and breakpoints are most
   reliable there.
3. Boot needs a real firmware dump in `assets/flash0/` (and optionally `assets/flash1/`), neither
   of which is in the repo.

## Why it opens on a disc error

The shell reaches the interactive XMB, but first shows "This disc cannot be started. The region
code is not correct." Cancel it with circle and the menu is there and usable.

Two separate things:

- **It thinks a disc is inserted**, because nothing in PPSSPP models an empty drive.
  `sceUmd.cpp` reports `PSP_UMD_PRESENT | PSP_UMD_READY` unconditionally, and `sceIoDevctl`
  `0x01F20001` (get disc type) always answers `0x10`, "game disc". JPCSP answers `0` when no ISO is
  loaded, which is why it never asks the next question. Fixing this properly means giving those two
  a notion of "no disc" - low risk for games, which always have one, but it is on a path every game
  uses.
- **It then asked whether the region matched and we didn't answer.** `sceIoDevctl` `0x01E18030` is
  the region check, and unusually it answers through the *return value* (1 matches, 0 doesn't)
  rather than an output buffer. It was unimplemented, so the VSH read the failure as a mismatch and
  retried 12 times. Implemented now - PPSSPP has no region-locked discs, so anything it can load is
  something it should run - and the call drops to one.

## Current status

`--vsh` boots VSH and renders real XMB frames. Measured on the GE command stream after 25
emulated seconds: 8 draw commands (two `DRAW SPLINE 16x9`, a `DRAW BEZIER 4x4`, four textured
`DRAW PRIM TRI_STRIP` - the wave background and icon quads), 41 texture binds, and full
PROJECTION/World/VIEW transform setup. `opening_plugin_module`, `sceMpegVsh_library` and
`impose_plugin_module` load and start.

**It reaches the interactive XMB.** Up to 2026-08-19 it showed the logo and wave animation and then
a full-screen red error screen, which turned out to be the XMB index failing to decrypt (below).
With that fixed, and with the disc region check answered (above), the shell opens on a dismissable
disc error and the menu behind it works.

Note that framebuffer readback does not work under headless on either backend -
`gpu.buffer.screenshot` and `gpu.buffer.renderColor` both answer "Could not download output" - so
checking what is actually on screen means running the app build. From headless, the useful proxy is
the per-frame display list signature (see the red error screen section): a screen that is finished
changing repeats byte-identically, and a live menu does not.

## Which firmware versions work

An earlier pass through this concluded "all of them", but what it measured was that the shell
*renders* - which several versions do while showing nothing but the wallpaper, or a full-screen
error. Re-measured by booting each installed firmware with `--vsh --graphics=software
--screenshot-save` and looking at the picture, an interactive XMB now comes up on **1.50, 1.52,
3.30, 3.40, 3.50, 3.51, 3.52, 3.95, 4.05, 5.03, 5.50, 5.55, 6.00, 6.20, 6.31, 6.39, 6.60 and
6.61**.

Still not there: **2.00, 2.60, 2.71 and 3.03** draw the wallpaper and the clock but never any menu
items, and **3.11** never gets as far as its first `sceDisplaySetFrameBuf` and leaves uninitialised
VRAM on screen. Those are open.

Note `--timeout` is wall-clock seconds, not emulated ones, and a shell that draws a full XMB runs
far slower than one stuck on a flat background - so a version that looks "stuck on the wallpaper"
at a short timeout may just need longer. 120 seconds is comfortable for all of them.

### What was in the way, in the order it was found

Four distinct causes, each of which stopped a whole band of versions:

- **`sceKernelLoadModuleVSH` under a NID we didn't have.** This is how the shell loads its own
  plugins, so without it nothing drawable ever loads and the screen stays black. It has five NIDs
  across the range. Same story for `sceKernelGetModel` (six) and `sceImposeSetStatus` (four); an
  unresolved GetModel meant the shell read a garbage model number and went looking for PSP-3000
  resources on a dump that has none.

  Finding them is mechanical, and worth writing down because it generalises. For each firmware,
  disassemble the module that exports the function and look for the body you already know from
  6.61: `sceKernelLoadModuleVSH` is the `modulemgr.prx` export whose callees are
  `sceKernelIsIntrContext`, `sceIoOpen`/`Ioctl`/`Close` and `sceKernelGetUserLevel`;
  `sceKernelGetModel` is whatever `vshbridge.prx` wraps in a `sceKernelGetUserLevel() < 4` check
  and re-exports. Cross-check the candidate against the boot log: the one that matters is the
  import the shell actually calls, which the "Unknown syscall (run)" lines name.

- **Missing `sceResmgr` keys.** `flash0:/vsh/etc/index_XXg.dat` is the index of what the XMB shows
  and is encrypted; a shell whose key is missing loads every resource successfully and still has
  nothing to put in the menu, so it gives up with the red error screen. Each generation has its own
  tag, one per PSP model from 5.03 on and a single unsuffixed `index.dat` before that.

  The keys are in each firmware's own `mesg_led*.prx`, in a table of 24-byte entries - 4-byte tag,
  16-byte key, 4 bytes of padding. Search the decrypted module for the tag as a little-endian word
  and read the next 16 bytes. **Validate the hit before believing it**: locate a tag that
  `PrxDecrypter.cpp` already has a key for and check that it matches byte for byte. Extracting the
  6.6x triple this way reproduces `keys_9DC14891_1/2/3` exactly, which is what established the
  layout in the first place. Don't try to walk the table blind - guessing the grid alignment
  produces plausible-looking garbage out of ordinary MIPS code.

- **`category_version` in the registry.** Our `sceReg` serves a compiled-in snapshot of a 6.6x
  PSP's registry, `/REGISTRY/category_version` included. Every shell checks it and treats a version
  higher than the schema it knows as a corrupt registry, offering to reset your settings instead of
  booting - which is what 1.50 through 5.50 did. The check is one-directional, older is always
  accepted, and nothing tried to migrate anything, so we report 1.

- **`sceRegCloseRegistry` dropping categories another opener still owned.** See `sceReg.cpp`; the
  VSH's alarm scan nests registry opens and this cost it its own open category.

### Sony renumbered the kernel NIDs, and that is what blocked everything below 6.60

Not offsets - NIDs. A function PPSSPP HLEs under its 6.6x `*_driver` NID is unrecognized on an
older build, so the import resolves to the **real firmware module** instead, and the real module
goes places the emulator can't follow.

| Function | 6.60/6.61 | 6.31-6.39 | 6.00-6.20 | 5.03-5.55 | 3.95-4.05 | 3.72-3.90 | 3.71 | 1.50-3.51 |
|---|---|---|---|---|---|---|---|---|
| `sceRtc_driver` `sceRtcSetAlarmTick` | `E09880CF` | `54B9C589` | `68AED59A` | `ADAF231F` | `55AC1C23` | `827BCB3F` | `329E8E3A` | `7D1FBED3` |
| `sceHprm_driver` `sceHprmReadLatch` | `E9B776BE` | `A3A87975` | `5FC5E53B` | `605DEA7A` | `A6E8D4F0` | `8C728076` | `F0AA1FB9` | `40D2F9F0` |

The rtc one is the interesting failure. Without the HLE, the VSH's alarm call ran the real
`rtc.prx`, which called on into `syscon.prx` and blocked forever on a `SceSysconSync` semaphore.
The symptom was a boot where every thread was parked and `idle0` was running, and the tell was a
**fourth** `SceSysconSync` waiter that a healthy boot doesn't have (there are three, one each for
sceSYSCON_Driver, sceRTC_Service and SceWlanMac - that's their normal idle state).
`hle.backtrace thread=<SCE_VSH_GRAPHICS>` named the whole chain: vsh_module -> vshbridge -> rtc ->
syscon -> wait. The hprm one is only a performance bug, but a loud one: the VSH reads the latch
once a frame, so an older firmware's 12-second boot logged ~20000 lines of the same import.

Two more of the same shape, for 1.50-2.xx: `sceImpose_driver` exports `sceImposeGetParam` as
`0x531C9778` and `sceImposeChanges` as `0xB415FC59` there, with no user-mode alias. Changes runs
once a frame too. And `ModuleMgrForKernel` numbers `sceKernelLoadModuleVSH` `0xA4370E7C` on 1.x -
that's how the VSH loads its own plugins, so unresolved it got module id 0 back and the
`sceKernelStartModule` after it failed, exactly the way `0xD5DDAB1F` did on 6.61 before it was
implemented.

**How to map a NID between versions.** Disassemble the same module from both firmwares with
`--re-module` and match by address. Don't compare raw addresses - the builds move code - anchor on
the plain user-mode export whose NID never changed (`sceRtc/0x7D1FBED3` for SetAlarmTick,
`sceHprm/0x40D2F9F0` for ReadLatch) and read off the `*_driver` NID at the same address. Below
3.95 `sceRtcSetAlarmTick` isn't in the user-mode library at all, so anchor on a neighbour instead:
it is the driver export immediately below `sceRtcIsAlarmed`. Where even that fails, match the
function body - that is how the two impose calls and the 1.x LoadModuleVSH were identified.

`sceRtcIsAlarmed` also had to be implemented (it returns 0, as in JPCSP). Left as a null entry it
returned `SCE_KERNEL_ERROR_LIBRARY_NOT_YET_LINKED`, and the 3.0x-3.5x VSH read that as "ask the
hardware instead" and went back to blocking on syscon.

### The scePaf heap pool, and where it lives

scePaf's allocator wants a pool base already written into one of its BSS slots. The module that
owns the allocator does fill that slot in itself, from its own module_start - but that start
thread hasn't been scheduled yet when vshmain makes its first allocation, so the pointer is still
null and the shell writes through it. Real hardware's kernel bootstrap starts these modules one at
a time and waits; `LoadAndStartVshKernelModules()` can't, so it pre-fills the slot instead.

Which module owns it moved. **Up to 4.05 the allocator is a separate `flash0:/vsh/module/heaparea1.prx`**,
which paf imports as `scePafHeaparea` and cannot allocate a byte without; from 5.01 it is compiled
into paf.prx and heaparea1 is gone. Loading heaparea1 when it's present was the missing piece for
every 3.x and 4.x version - without it `scePafHeaparea_ACCE25B2` was an unresolved import, paf
built a heap out of an uninitialized stack pair, and vshmain died storing the null the allocator
handed back.

Either way the slot is the second of the two pool pointers that module's init fills in with
`sceKernelTryAllocateFpl`, and either way its offset from the module base moves with every build
while its offset from gp does not:

| Module | gp - slot | Checked against |
|---|---|---|
| `paf.prx` | `0x7E88` | 6.00, 6.20, 6.31, 6.37, 6.39, 6.60, 6.61 (base-relative 0x18CCD8..0x18D728) |
| `heaparea1.prx` | `0x7FCC` | 3.95, 4.05 |

To re-find it in a build not listed: disassemble the module, find the one function that calls
`sceKernelTotalMemSize`, and read the address handed to the **second** of its two
`sceKernelTryAllocateFpl` calls as `a1`. The shape is identical in both modules - TotalMemSize, a
`> 0x2400000` test picking 0xA00000/0xC50000 pool sizes over the compiled-in defaults, then two
`sceKernelCreateFpl` + `sceKernelTryAllocateFpl` pairs writing to adjacent slots.

### 1.50 declares no module attributes

`g_runningVSH` was set from `PSP_MODULE_VSH_MODE` in the module's attribute word. 1.50's
vshmain.prx has attribute `0000` - Sony only started setting the flag in 1.52 - so the entire VSH
bootstrap was skipped and the shell ran with none of its support modules loaded. The check also
accepts the module *name* `vsh_module` now.

### The vsh_module alarm-category patch only 6.6x needs

That offset is in rodata, so there is no gp anchor for it; instead the patch only fires when the
word at `+0x455C4` is the `0x3F666666` it was derived from. On 6.39 that word is a different
float, and on 6.20/6.00 it is ASCII string data (`5f746c75`, `776f6461`) - the old unconditional
write was corrupting a string table on those. Every version below 6.60 reaches the XMB without the
patch, which is itself a hint that whatever precondition makes that scan safe on real hardware was
lost somewhere between 6.39 and 6.60.

### Kernel modules with per-model builds

`LoadAndStartVshKernelModules()` asked for `memlmd_01g.prx`, `loadexec_01g.prx` and
`wlanfirm_01g.prx` by name. A firmware unpacked for one model ships only that model's build, and
PPSSPP's own updater unpack defaults to 02g, so all three failed to load. `ResolveVshModelModule()`
now substitutes the emulated model's suffix when that file exists, falling back to `_01g` for a
dump unpacked with model `any` (which has every model's). Firmwares older than about 3.60 predate
the PSP-2000 and have no per-model split at all, so they are unaffected.

### 5.55 needed two PRX keys

Its `flash0:/kd` modules are tagged `0x4C941AF0`/`0x4C941BF0`, and those two were the only entries
of JPCSP's PRX tag table PPSSPP was missing. The shell came up anyway - vshmain and paf are user
modules - but with not a single driver behind it.

## The red error screen

What is known, all measured from a 40-emulated-second `--vsh` boot:

- **Nothing fails to load.** Every `sceIoOpen` in the whole boot succeeds: `flash0:/font/*.pgf`,
  `opening_plugin.rco`, `system_plugin.rco` / `_bg` / `_fg`, `topmenu_plugin.rco`,
  `topmenu_icon.rco`, `impose_plugin.rco`, and `flash0:/vsh/etc/index_02g.dat`. The only I/O
  failure in the entire trace is `flash1:/registry/init.dat`, which is correct (see above). So VSH
  genuinely gets as far as building the top menu before giving up.
- **Where it freezes.** Grouping `sceGeListUpdateStallAddr` by display list id gives a per-frame
  signature of what is being drawn. It grows 6 -> 10 -> 11 -> 15 -> 19 -> 33 -> 34 -> 38 stall
  points (the dialog and its text being composed) and then repeats **byte-identically for the
  remaining ~2016 frames**. That is the error screen locking in; everything before it is animation.
- **Nothing logs an error in the run-up.** The window before the freeze is just the startup jingle
  decoding (`sceAtracDecodeData` + `sceAudioOutputPannedBlocking`), `vshCtrlReadBufferPositive`,
  `scePowerIsSuspendRequired`, `sceKernelGetUserLevel` and `sceImposeChanges`. So the trigger is a
  *wrong return value* we hand back, not a missing call - which makes it much harder to spot than
  the `sceKernelLoadModuleVSH` bug was.
- **`sceImposeChanges` is not a symptom of the error.** It runs once a frame regardless; the
  ~10000 calls per boot are just the indicator refresh, and they start *after* the frame loop is
  already steady. Don't read into it.

### What actually triggers it: the XMB index can't be decrypted

Found by raising log channels for a narrow window around the transition (5.8s to 6.7s emulated,
~12K lines - `log.channel.set`, not a full-boot log). The frame loop's calls before and after the
transition differ sharply: before it there is resource loading, module starting and registry
access, after it only the clock and battery indicators. Inside that window:

```
sceIoOpen(flash0:/vsh/etc/index_02g.dat)   -> fd 8
sceIoRead(8, 092a2d40, 496)                -> the whole file
sceIoClose(8)
unresolved import sceResmgr/9dc14891, called from 'vsh_module'    <- traps here
sceKernelExitDeleteThread(1)               <- the ScePafJob building the menu gives up
```

`index_02g.dat` is the index of what the XMB shows, and it is encrypted (it starts `PSPsysGP`, not
the `release:` of an already-plaintext one). `sceResmgr_9DC14891` is what decrypts it. With that
import unresolved the call trapped, the index stayed encrypted, and the job thread quit - hence a
shell that has loaded every resource successfully and still has nothing to show.

**Fixed.** `Core/HLE/sceResmgr.cpp` implements the call, the three tags it needs
(`0x0B2B90F0/91F0/92F0`, keys and code `0x5C` from JPCSP) are in `Core/ELF/PrxDecrypter.cpp`, and
`pspDecryptType9()` there decrypts it - 496 bytes in, 159 out, starting `release:`, which is the
check `sceResmgr` logs on every call.

Type 9 turned out to be type 6 with three differences, all because a type 9 file carries a real
ECDSA signature at `0x104..0x12C` where type 6 has nothing:

- The "this region must be empty" check has to stop at `0x104` rather than `0x10C`, or the first
  8 bytes of the signature fail it (this was the original `-2`).
- The signature is excluded from the hashed header rather than fed into it - JPCSP zeroes
  `buf2[0x34..0x5C)`, which is the same range after its header rearrangement, so `PRXType9` simply
  leaves that field zero.
- `ecdsa_hash` in the KIRK CMD1 header stays 0. Type 6/7 set it; the branch type 9 takes writes
  only the mode word. Setting it was the final `-4`.

The SHA1 check inside the decrypt is a good progress signal while porting: reaching `-4` means the
header reconstruction and hash inputs were already right and only the KIRK call was wrong.

## The root cause of the long-standing blank screen

**`ModuleMgrForKernel/0xD5DDAB1F` (`sceKernelLoadModuleVSH`) was unimplemented.** That is how VSH
loads its own plugins: the XMB's UI lives in `flash0:/vsh/module/*_plugin.prx` and `vshmain`
pulls those in through this kernel call, not the user-mode `sceKernelLoadModule`. The import went
unresolved, vshmain got no module id back, and the `sceKernelStartModule` that followed was called
with **id 0** and failed with `0x8002012E UNKNOWN_MODULE`. No plugin ever ran, so the scene had
its containers but nothing drawable in them - which is exactly the "render state set up 6 times
per frame, zero draws" symptom this investigation chased for a long time.

Implemented along with `0xD86DD11B` `sceKernelSearchModuleByName`, the other unresolved
`ModuleMgrForKernel` import.

Found by scanning a whole boot for *any* HLE call returning an error. Exactly one mattered, and it
had been missed on two earlier passes because it logs as `: error 8002012e` rather than as a
`8xxxxxxx=sceSomething(` return value. **Grep both shapes.**

## Solved along the way

- **Real kernel driver modules running as genuine MIPS code** needed scratchpad RAM kernel-mode
  address mirrors (`Core/MemMap.cpp`), dummy COP0 instructions (`Core/MIPS/Interpreter.cpp`), and
  a GPIO + Syscon serial MMIO model (`Core/HW/GpioMMIO.{h,cpp}`) so `kd/syscon.prx`'s real
  handshake completes instead of spinning.
- **`vsh_module` alarm-task SIGSEGV**: a targeted data patch zeroing an "alarm category count"
  field nothing else initializes (`__KernelLoadELFFromPtr`). Symptom patch, not a principled fix -
  why real firmware never reaches that path was never established.
- **`SCE_KERNEL_ERROR_NO_MEMORY` in the app build**: the 4MB kernel pool was exhausted by PPGe's
  ~2MB overlay texture (now skipped when booting VSH) plus 11 driver modules each asking for a
  256KB thread stack (now given 32KB).
- **`sceHprm_driver/sceHprmReadLatch` (0xE9B776BE)** implemented and its output struct actually
  zeroed, matching JPCSP. Correct, but did not affect the blank screen.
- **flash1 is now mounted** when a dump sits next to the flash0 one. `sceReg` serves a compiled-in
  snapshot of some real PSP's registry (`Core/HLE/sceReg.cpp`) rather than reading the volume, and
  a genuine dump has only `registry/system.dreg` and `.ireg`. **`flash1:/registry/init.dat` must
  stay absent** - an earlier note here said creating one "changes nothing", which is wrong. VSH
  stats it twice during startup, and *finding* it means "reinitialize the registry": it then calls
  `sceRegRemoveRegistry` and `sceIoDevctl("flashfat1:", 0x5802)`, both `UNIMPL`, and the boot dies
  without starting a single plugin module. Its absence is the healthy path, and the resulting
  `FILE NOT FOUND` in the log is expected, not a lead.
- **A diagnostic-tooling bug that produced a phantom lead.** The "unresolved import" reporter
  encoded identity in a 55-slot ring buffer, but a VSH boot registers ~1000 unresolved imports, so
  the ring wrapped ~18 times and every report named whatever most recently reused that slot. It
  made `sceMgVideo_driver` look like one massively hot call when it was the misattributed sum of
  many. Fixed by recovering `stubAddr` via `g_lastSyscallPC` and looking identity up in
  `module->importedFuncs`, which already tracked exactly this (interpreter-only, matching the
  original limitation). **Lesson: when identity has to be recovered later by address, check
  whether something already tracks it before adding a cache.** Breakpoints were never at fault -
  if one doesn't fire, suspect the address first.

## Imports that must stay unresolved

Counterintuitive but repeatedly measured: implementing more of what the VSH imports can make the
boot *worse*, because a correct implementation lets a real flash0 driver finish its init and walk
on into hardware PPSSPP does not emulate. Leaving the import unresolved makes the driver fail early
and the boot continues. All three are commented in the source at the point where they'd be added:

- **`ThreadManForKernel` mutex/fpl NIDs** (`0xB7D098C6` `sceKernelCreateMutex`, `0xB011B11F`
  `sceKernelLockMutex`, `0x6B30100F` `sceKernelUnlockMutex`, `0xD979E9BF` `sceKernelAllocateFpl`) -
  we implement all four for user mode already. Resolve them and the NAND and ID storage drivers
  init successfully, then `sceIdStorage_Service` polls the NAND controller at `0xbd101300` forever.
  No module starts.
- **`InterruptManagerForKernel` interrupt registration** (`0x58DD8978`, `0xF987B1F0`, `0x4D6E7305`,
  `0xD774BA45`) - JPCSP can honour these because it emulates the interrupt controller as MMIO. We
  dispatch the few interrupts we emulate ourselves (`__RegisterIntrHandler`), so success is a lie
  the drivers act on. Measured: 31 such calls (interrupts 4, 12, 15-18, 20-24, 31), then a stall in
  GE list execution with no plugin module started.

73 unresolved import hits over 37 distinct module/NID pairs remain in a boot. Most of the rest are
`sceSysEventForKernel`, `sceSuspendForKernel` and `*_driver` modules that would need real hardware
behind them. Note the *runtime* ones log as `Unknown syscall (run) at ...: unresolved import ...`
(`HLE.cpp:905`) - a different string from the load-time `Unknown syscall from known HLE module`,
so grep for both or you will undercount badly.

## How this boot path works

Real hardware boots VSH through the kernel's own module bootstrap, launched from
`flash0:/reboot.bin`. PPSSPP doesn't emulate that chain. `--vsh` loads
`flash0:/vsh/module/vshmain.prx` through the normal PRX loader, and
`LoadAndStartVshKernelModules()` (`Core/HLE/sceKernelModule.cpp`) approximates the missing
bootstrap by loading and starting `vshbridge.prx`, `paf.prx`, `common_gui.prx`, `common_util.prx`
and 11 real `kd/` drivers first, in the order JPCSP's own `--vsh` shortcut uses.

JPCSP's `--vsh` turns out to use the same shortcut rather than a real reboot.bin LLE path, so this
is a legitimate if approximate route to a working VSH.

Known differences from JPCSP's `--vsh` that have **not** been tried: it preloads
`PSP_MODULE_AV_VAUDIO`, `AV_ATRAC3PLUS` and `AV_AVCODEC`, and sets the io filepath to
`ms0:/PSP/GAME`. Its trick of forcing the root thread to kernel mode at priority 0x7E **was**
tried and reverted - it reorders scheduling enough that `sceVshBridge_Driver` runs before a
precondition is ready. Firmware version already matches (660 both sides).

## Thread structure

There is no `vsh_main` thread and there shouldn't be. `vshmain.prx`'s `module_start` runs on the
`root` thread, creates and starts `SCE_VSH_GRAPHICS` (entry `08818d14`) and returns from module -
visible in the log as `Context switch: root -> ... (returned from module)` right after the
`sceKernelStartThread`. **`SCE_VSH_GRAPHICS` is VSH's main thread**, and its entry is the
outermost frame of every render backtrace.

Steady state is 8 threads: `SCE_VSH_GRAPHICS`, `ScePafThread`, `SceWaveMain`, `SceWlanMac`,
`sceRTC_Service`, `sceSYSCON_Driver`, `idle0`, `idle1`, plus transient `ScePafJob` threads that
load the `.rco` resources and exit. The frame loop is healthy: poll `ScePafSyncCall`,
`vshCtrlReadBufferPositive`, render, `sceDisplaySetFrameBuf`, wait `SceVblankSync`.

## The render call chain

Still worth having if individual widgets misbehave. Traced with a **write memcheck on a display
list word** (`memory.breakpoint.add address=... write=true`), which stops with a structured hit
and lets `hle.backtrace` name the whole chain - minutes of work, and the technique to reach for
first next time:

```
08818d14   SCE_VSH_GRAPHICS thread entry
  08895544   6-iteration loop
    08963cfc   container: sets base render state, walks children at [obj+0x550], count [obj+0x554]
      08950754   per-child render
        089a1cdc / 089a36e4 / 089a38c0 ...   GE state helpers
```

`08950754` gates on `z_un_089501cc(child+0x450) == 1` and on `[child+0x438] != 0`, then computes
the scissor by intersecting `[child+0x43c..+0x448]` with a parent clip rect.

## Tools and methodology

- Everything useful works on a live session: `hle.thread.list`, `hle.eventflag.list`,
  `hle.backtrace`, `memory.disasm`, `memory.readString`, `gpu.displaylist.disasm`, `cpu.evaluate`
  (expressions like `[s0+0x438]` read guest memory), and memory breakpoints. See
  `docs/WebSocketDebugger.md` for the catalog.
- `cpu.runUntilTime us=N` is what makes a script reproducible - it stops on the requested emulated
  microsecond, so every run reaches the same instruction. Ask for the time you need and no more;
  the red screen locks in around 6 emulated seconds.
- Raise log channels for a *short* window only - `sceGe`/`G3D` at debug produce ~200K lines in
  three seconds of boot. `log.channel.set` is session-only, never persisted. `--loglevel=3` is a
  good default for the whole run: bare `--log` on a VSH boot writes ~2.3GB, level 3 writes ~66KB.
- Diffing against JPCSP (`../JPCSP`, Java source, NIDs searchable by hex) repeatedly beat guessing
  at missing HLE functions. `assets/flash0` has the decrypted PRXs if a NID needs disassembling.
- Headless is preferred over the app build - the app's Debug config blocks on a Windows crash
  dialog with no console output.

**Emulation is not the slow part.** A VSH boot runs at roughly real time even in a Debug build
(1 emulated second in a 2.5s run that includes process startup). If a scripted run takes minutes,
it is waiting, not emulating - and the usual cause is an explicit `:wait cpu.stepping` after a
`cpu.runUntil*`/`cpu.resume`/`cpu.step*`, which `--sync` already waits for. The redundant one waits
for a second `cpu.stepping` that never comes and burns the whole `--sync-timeout`. See
`Tools/wsdbg/README.md`.

## Not included on this branch

The separate `--vsh-reboot` experiment (loading `flash0:/reboot.bin` for a real LLE kernel boot,
with NAND/KIRK/system-control MMIO emulation) lives on `vsh-experiments`. The direct HLE path here
reaches much further without any of it.
