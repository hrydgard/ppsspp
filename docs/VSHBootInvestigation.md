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
