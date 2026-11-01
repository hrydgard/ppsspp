# Debugging PPSSPP (agent notes)

How to drive PPSSPP's debugger facilities from a script or agent session, and the traps around them.
The WebSocket protocol reference itself is in [WebSocketDebugger.md](WebSocketDebugger.md); the
threading rules for debugger code are in [DebuggerThreading.md](DebuggerThreading.md).

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
(see [Building and testing](building.md)), then run it with `--debugger=PORT` and something that keeps running/looping so the
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
./Windows/x64/Debug/PPSSPPHeadless.exe --cpu=interpreter --debugger=34567 --timeout-wall=100000 --graphics=software --log \
    --root pspautotests/tests/../ pspautotests/tests/cpu/cpu_alu/cpu_alu.prx > hl.log 2>&1 &
# wait for "Listening on port" in hl.log, then:
./Tools/wsdbg/target/release/wsdbg.exe 34567 --sync --sync-timeout 15 < script.txt
```

- **The timeouts apply to the whole session**, not per test. `--timeout-wall=N` is real seconds and
  `--timeout-emulated=N` is emulated ones; both default to infinity, both can be set at once, and whichever
  is reached first ends the run (`TIMEOUT` or `TIMEOUT (emulated)`). With `--state=`, emulated time counts
  from when the state has loaded: until then the game boots for however long the load takes on the host, so
  counting from boot made runs end on different frames. `--timeout` is the old name for
  `--timeout-wall`. As soon as you pass one it applies to your whole interactive debugging session too, so
  pass something huge (`--timeout-wall=100000`) or the process exits out from under you mid-session. The
  wall-clock check is skipped while `IsDebuggerPresent()`; the emulated one doesn't need that escape hatch,
  since sitting at a native breakpoint burns no emulated time.
- **Which one you want depends on the question.** Wall-clock is what stops a hang from hanging the machine.
  Emulated is what you want for "has the game had long enough to get somewhere" - a heavy scene runs many
  times slower than real time and a near-idle one much faster, so the same wall-clock budget means very
  different amounts of game time. Booting a firmware VSH to its XMB is a good example: 10 emulated seconds
  is about 25 real ones on 6.61 and about 7 on 2.00.
- **Prefer `--debugger=0` and scrape `Listening on port N` from that run's own log** over hardcoding a port. Also
  `taskkill //F //IM PPSSPPHeadless.exe` between runs for hygiene (Git Bash here has no `pkill`) - leftover
  instances are easy to accumulate when a script leaves the CPU stopped at a breakpoint.
- **On Linux, kill leftovers with `pkill -x PPSSPPHeadless`, never `pkill -f PPSSPPHeadless`.** `-f` matches full
  command lines, and the shell running your `pkill` has "PPSSPPHeadless" in its own command line, so it kills
  itself (exit 144) and the emulator instances you meant to clear survive into the next run.
- **Attaching gdb to a running instance fails under WSL/Ubuntu** (`ptrace_scope`: "Could not attach to process"),
  so a hang has to be caught by starting the run under gdb. Interrupting a batch-mode gdb from another shell with
  `pkill -INT` never got as far as the backtrace in the 2026-09-22 attempts. Before reaching for gdb, first rule out
  the cheap explanations below (backend, flags) by diffing a good and a bad command line.

  Some history, because it silently produced a round of bogus results before it was fixed: `Common/Net/HTTPServer.cpp`
  used to set `SO_REUSEADDR`, which on Winsock means "allow binding a port someone else is already listening on"
  (unlike POSIX, where it only covers TIME_WAIT). Two instances would *both* bind the same explicit port and *both*
  log `Entering web server loop. Listening on port 34567`, with the winner of any given connection undefined - so a
  client aimed at a fixed port could end up driving a leftover process running a different binary, CPU backend, or
  game. It's `SO_EXCLUSIVEADDRUSE` on Windows now, so the second bind fails honestly, and a non-zero `--debugger=PORT`
  that can't be honored is fatal in headless (exit 1) instead of falling back to a random port. If you still suspect
  you're talking to the wrong process, the `version` response carries `pid` and `path` - check them.
- **The headless build defaults to JIT** (`Headless.cpp`, `CPUCore cpuCore = CPUCore::JIT`), despite
  `g_Config.iCpuCore` being force-set to INTERPRETER just above - `ApplyToConfig()` has the final say. Pass
  `--cpu=interpreter` for the interpreter. (`-r` is `--root`, which eats the next argument as the root path.)
- When a test finishes, headless exits and the WebSocket connection closes (`CloseFrame { code: Away }`). So "the
  connection just closed" after a `cpu.resume` normally means **the breakpoint you were counting on never tripped**
  and the game ran to completion - not a transport problem.
- Some events deliberately never respond while the CPU is stepping, so `--sync` will burn its full timeout on them:
  `gpu.stats.get` and `gpu.stats.feed` (documented - they answer after the next flip), `gpu.record.dump`, and
  `input.buttons.press` (waits for N frames). Send them with wsdbg's `:nowait`, and let the next line
  (a `cpu.runUntilTime`) run the CPU:
  `:nowait input.buttons.press button=circle duration=4`. `duration` counts vblanks, not seconds.
- **Don't send a bare `cpu.resume` in a `--sync` script.** `--sync` waits for the CPU to stop again, which
  a free run never does, so it burns the whole `--sync-timeout` while headless runs unthrottled, which can
  be hundreds of emulated seconds. Use `cpu.runUntilTime`.
- Log broadcasts drown scripted output. Pass `--quiet` to wsdbg, which turns them off.
- **Nested parameters work in wsdbg's `key=value` shorthand**: values are parsed as JSON, and single quotes keep
  the inner double quotes, e.g. `input.buttons.send buttons='{"cross":true}'`. Prefer that over a raw JSON line,
  which gets no ticket (see below).
- Keep wsdbg scripts in files and pipe them in, rather than building JSON inline in a shell command - inline
  `{"event":...}` in a bash heredoc trips Claude Code's command analyzer ("brace with quote character") and forces a
  manual approval prompt for every single invocation.
- **`--sync` can only match a response to a request that carries a ticket**, and wsdbg only assigns tickets to its
  `key=value` shorthand. A raw JSON line without a `ticket` isn't waited for at all, so use the shorthand (nested
  values included, see above). Hex works there: `memory.disasm address=0x08804000`.
- **To see the screen from a script, use wsdbg's `:screenshot <file.png>`** while the CPU is stopped, e.g. after a
  `cpu.runUntilTime`. With headless, run `--graphics=software` for it: headless Vulkan has no output image to read
  back and asserts. Give a native path on Windows (`C:/...`, not `/c/...`).
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
  it away rather than flushing. To actually read a crash trace, give that run a short `--timeout-wall` and `wait` for
  the process to exit on its own.
- **`0xFFFFFFFF` is not an invalid instruction** - it decodes to `vflush`, a real Allegrex VFPU op, so writing it
  over code to test illegal-instruction handling just runs it. Check what an encoding actually is with
  `memory.disasm` before assuming it's garbage; the interpreter raises `ExecExceptionType::ILLEGAL` only when
  `MIPSGetInstruction` has no interpreter for it (`tge`/`tlt`/`teq` and friends).
- **To line input injection up with a wall-clock repro, use `cpu.status`'s `us` field** (emulated microseconds), not
  `ticks`. The PSP's clock frequency is changeable and games do change it - CrossCraft Classic runs at 333MHz, so
  `ticks / 222000000` is off by a factor of 1.5. `clockHz` is reported alongside.
- **`--graphics=software` cannot see "the screen stops updating" bugs.** When HLE writes pixels straight into a
  display buffer - video decoding, `sceJpeg`, `sceMpegAvcCsc` - the hardware backends only find out because the HLE
  calls `gpu->PerformWriteFormattedFromMemory()`. The software renderer reads that memory directly and so renders
  the frame whether or not anything was notified. A missing notification therefore looks perfect under
  `--graphics=software` and shows up as a frozen screen on every real backend, while the decode logs keep scrolling
  past as if all were well. Reproduce display bugs on a hardware backend (see "Choosing a GPU backend" below;
  `directx9` is not a valid value) and compare `--screenshot-save=` output, not the log.
- **`wsdbg --launch` only works with the headless build.** The app build is a GUI-subsystem exe with no stdout, so
  the `Listening on port N` line never reaches the launcher and it gives up. Start it yourself with an explicit
  `--debugger=PORT` and point wsdbg at that port. Note also that `--debugger-run` is `CmdLineMode::Headless`; the
  app takes plain `--debugger=PORT`.
- **`--launch` needs `--log` to find the port.** Without it, headless prints only emulated printfs, so the
  `Listening on port N` NOTICE never appears and `--launch` fails with "PPSSPP never reported a debugger port".
  `--log --loglevel=3` keeps the port line while cutting the debug flood - full `--log` at the default level costs
  a lot of emulation speed (a 25-second run of a game playing video wrote 450k lines and ran several times slower
  than real time, which on its own looks like the stall you're hunting).

## Measuring a commercial game with headless

Headless will happily run a game and print nothing, or run *a different configuration than the one you asked
for*, and both look like a clean pass if you are counting log lines. Four traps, each of which silently
produced a round of bogus results here:

- **`--log` is required for any log output at all**, and the log goes to **stderr**. Without it you get only
  headless's own lines (`Loaded State`, `TIMEOUT`); `-d`/`-v` set the level but don't turn the printf logger
  on. So folding stderr in isn't optional if you're grepping, which is what makes the third trap bite.
- **Headless's memory stick is not the app's.** It defaults to `<exe dir>/memstick` (`headless/Headless.cpp`),
  so `Windows/<platform>/<config>/memstick`, while the app derives its own from `installed.txt` or Documents
  (`InitMemstickDirectory` in `Windows/main.cpp`, which carries a TODO about sharing the derivation). Headless
  *creates* that directory, empty `PSP/NAND/flash0` and all, so firmware installed through the app is invisible
  and every LLE module quietly falls back to HLE - at which point you are measuring the HLE you were trying to
  compare against. Pass `--memstick=` explicitly; it resolves relative to the CWD, not the exe.
- **Check the exit code.** An unrecognised parameter prints `Error: ...` to stderr and exits 1, which is
  correct and easy to throw away: fold stderr into stdout (which the first trap forces), count grep hits, and a
  run that never started reports the same all-zero line as a clean one.
- **Zero is not a pass.** Measuring by log-grepping needs a positive precondition asserted separately ("did
  this run reach the code at all"), or "0 errors" also means "0 anything" - which is equally what a failed
  boot, a savestate that never reaches the cutscene, and a silent HLE fallback produce.

The last three compound: the fix is to treat the run's exit code and a positive "we got here" counter as
preconditions, and only then believe the error counts.

Rather than silently falling back to HLE, headless refuses the run: an explicit
`--disable-hle=` whose firmware module isn't there names the module, prints the `flash0:/kd` and memory stick
it looked in (usually enough to spot that it's the one beside the exe), and exits 1. Only an *explicit*
`--disable-hle` binds - sceMpeg and sceMp4 are LLE by default and still fall back quietly, or every run on a
machine with no firmware would fail. So when a measurement depends on the real module, pass the flag
explicitly even though it's on by default, and the run will tell you if it didn't get it.

`--force-hle=` is the other direction, and the one to reach for when deciding whether something is
our fault: it puts our HLE back for libraries that now run the real module by default, so the same
repro can be run both ways and the logs diffed. That is how the leftover warnings in Tekken 6 were
sorted - three appeared identically with `--force-hle=16`, which made them the game's own, and the
fourth only under the real module, which made it ours. Neither `--nand=` pointing somewhere empty
nor `--appendconfig` does this job: the firmware gets found anyway and the setting is per-game.

### Choosing a GPU backend

- **`--graphics=software`** (the default) needs no GPU at all, and matches the GPU pspautotests' reference
  screenshots (taken on a PSP) best. It's slow for games, though: it runs display lists synchronously inside
  `sceGeListEnQueue`, which then dominates any profile of the emulator thread.
- **`--graphics=vulkan`** renders offscreen, into images of its own with no window or swapchain, so it needs no
  display. On macOS it goes through MoltenVK.
- **`--graphics=opengl`** renders offscreen on macOS (a CGL context with a framebuffer object of its own), and
  through a hidden SDL window elsewhere.
- **`--graphics=d3d11`** (Windows) renders through a hidden window.

For game runs, prefer a hardware backend: 30 emulated seconds of God of War take 3-4 seconds instead of a minute.
The pspautotests pass on Vulkan and OpenGL except for 17 GPU tests, whose references are hardware screenshots
that the hardware backends don't match exactly.

To smoke-test a CPU or GPU change on real games, run the old and the new binary on the same games with
`--timeout-emulated` and `--screenshot-save=`, and compare the screenshots. Most games are deterministic from
run to run, so a different screenshot means different behaviour. God of War: Chains of Olympus isn't: its menu
animation follows wall-clock time, so even the same binary lands on different frames.

To check that a game keeps running after `--state=` loads, pass `--frame-stats`: at the end of the run it prints
the flips in each emulated second since the load. A stuck or crashed game shows as zeros or a short list, and a
steady game as a flat 30 or 60. The state's load result is printed too, and a failed load exits 1.

To get past menus, `--press=SECONDS:MASK[:DURATION],...` holds buttons at emulated times (`MASK` is a sceCtrl
mask, `0x8` START and `0x4000` CROSS, held 0.15 seconds by default), and `--screenshot-at=SECONDS:FILE,...` saves
screenshots along the way.

For audio, `--dump-audio=FILE.wav` records the mixed output (before the game volume setting) for the whole run.
An audio glitch you can hear usually shows in the waveform: a click is a jump between neighbouring samples, and
a repeated or dropped mixer block is two identical 64-sample runs in a row.

Pass `--graphics` explicitly even when you want the default, so a copied command line doesn't depend on it. If a
run goes silent with no CPU use, a host thread is blocked, and neither `--timeout-wall` nor `--timeout-emulated`
will end it, as both are only checked when the emulation loop comes around.

### Measuring speed

`Tools/headless_bench.py` compares the speed of headless binaries on savestates or frame dumps. It alternates
the binaries from round to round and reports medians of four numbers per binary:

- **Wall time** is what a player sees, but it swings by several percent between identical runs (heat, the
  scheduler moving threads between performance and efficiency cores).
- **CPU time** (all threads) says how much work was done. A change that keeps it but cuts wall time spreads
  the work better, and one that cuts it while wall time stays has left the threads waiting.
- **Instructions retired** vary by well under 0.1% between identical runs, so they show changes too small for
  wall time.
- **Cycles** also count stalls (a division, a missed cache line) that instructions don't.

The last two come from `/usr/bin/time -l` on macOS and `perf stat` on Linux. The tool checks for mains power
first: on battery (or in macOS low power mode) the clock speed varies and the numbers aren't comparable. Stop
anything else that uses the CPU, including a forgotten `usbhostfs_pc`, which can spin a core indefinitely. A run
that exits with an error, misses `TIMEOUT (emulated)` or doesn't load its savestate stops the tool rather than
passing as a fast run.

To measure the CPU emulation alone, use `--graphics=null` (headless only, `GPU/Null/GPU_Null.cpp`). It runs
display lists, bounding boxes, matrix loads and block transfers, moves the vertex and index pointers past
each draw, and charges the same GE time per draw, but draws nothing and creates no draw context. Games flip
the same frames as with Vulkan, in roughly half the host instructions. Nothing is rendered, so screenshots
and anything that reads a framebuffer back through the GPU (rather than from PSP memory) get nothing.

Headless decodes vertices with the JIT like the app, where the host can (`--vertex-jit=false` for the C++
decoder, which the tests don't need either).

### Profiling the CPU cores

`--jit-profile=FILE` (app and headless) samples the emulator thread's host PC every 50 µs and, when the JIT
goes away or clears its cache, appends a report to FILE:

- the share of samples in JIT blocks, in the fixed routines they call (like the dot product routines), in
  the dispatcher, and outside JIT code by host function (with symbols where `dladdr` finds them);
- the 30 hottest blocks, each with its MIPS code, its IR, and its host code with the share of samples per
  host instruction, the IR instruction each comes from, and the samples in routines charged to the call;
- the hot fixed-code routines, per instruction.

It costs nothing when off. The native JITs (macOS, Linux and Android by signal, Windows by
`SuspendThread`; RISC-V and LoongArch on Linux only) sample the host PC; the IR interpreter instead times every block, which used to be the
`IR_PROFILING` define. Short runs from a savestate include compiling everything after the load, which
shows as emitter and IR pass functions; profile 20 emulated seconds or more to see the game itself.
To compare two builds by where the time goes, profile one game at a time: several at once wait on each
other's GPU work, which moves the shares.

## Debugging and breakpoint considerations

It might be worth trying the interpreter - all types of breakpoints are the most reliable with this CPU backend.
The JITs are much, much faster and in theory also support breakpoints, and we're trying to make the JITs
as reliable, but are maybe not quite there.

Concretely, as measured against the headless build (2026-08-16), per CPU backend:

| | interpreter | JIT / IR JIT |
|---|---|---|
| `cpu.breakpoint.*` (exec) | works | works |
| `cpu.stepInto/Over/Out`, `runUntil`, `nextHLE` | works | works |
| `memory.breakpoint.*` (memchecks) | works | **only for constant addresses** |
| `cpu.regBreakpoint.*` | works | never trips (as documented) |

## Native debuggers and the memory fault handler

With fast memory on, a bad guest access from JIT code is a real host SIGSEGV/SIGBUS (an access violation on
Windows), which `Memory::HandleFault` (`Core/MemFault.cpp`) catches and turns into a clean emulator-side
exception. A native debugger sees the fault first and stops there, so a run that would have reported
`Read Word: SIGSEGV at ...` looks like a crash instead. Continuing hands the fault to the handler, like a
first-chance exception in Visual Studio. To keep the debugger out of the way when the faults are expected:

- **lldb (macOS/iOS)** needs both of these, verified 2026-09-28 against `pspautotests/tests/cpu/crash`. The
  first skips the stop on the Mach-level EXC_BAD_ACCESS, the second the stop on the signal it turns into:

  ```
  settings set platform.plugin.darwin.ignored-exceptions EXC_BAD_ACCESS
  process launch --stop-at-entry
  process handle SIGSEGV SIGBUS -s false -n false -p true
  continue
  ```

  (`process handle` needs a live process, hence the `--stop-at-entry`.) The catch is that a genuine host crash
  then kills the process without stopping in the debugger.
- **gdb (Linux/Android)**: `handle SIGSEGV nostop noprint pass`.

The `pspautotests/tests/cpu/crash/crash_*.prx` binaries each make one bad access (read, write, float, bad jump),
which makes them the quickest check that the handler works on a platform: run one with `--cpu=jit` in headless. They
should print the guest exception and exit 0, not die with signal 11.

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

## Finding a game function to replace by hash

The tell is a game that's right on the software renderer and stale on the hardware backends, usually a video
that sticks on its first frame while the audio plays on. The game is writing pixels into VRAM with the CPU, and
the hardware backends only see that when it goes through something we hook. Games ship their own `memcpy`,
`memset` and so on, and we recognise known ones by a hash of their code: the `hardcodedHashes` table in
`Core/MIPS/MIPSAnalyst.cpp` names them, and `Core/HLE/ReplaceTables.cpp` swaps in a replacement
(`Replace_memcpy` hands VRAM copies to `gpu->PerformMemoryCopy`). SSX On Tour's videos were fixed this way.

Finding the function:

1. **Get to the right moment.** Confirm the thing is actually happening before breaking on it - SSX's
   videos only start 11.5s in, and a screenshot from earlier just shows the paper background. `memory.read` of
   the decode buffer at a few `cpu.runUntilTime` points tells you when it starts changing.
2. **Break on the write**, under the interpreter (`--cpu=interpreter`), since JIT memory breakpoints only work for constant
   addresses: `memory.breakpoint.add address=<a pixel mid-framebuffer> size=4 write=true enabled=true`, then
   `cpu.resume`. Pick a pixel in the middle of the frame, not the first one, which clears also hit.
3. **`hle.backtrace`** gives the function's entry address. Addresses come back
   as decimal in JSON; convert carefully (`145311076` is `08a94564`, not `08ab1564`).
4. **`memory.disasm` the whole function** and check that it really is what you're going to call it: argument
   order (`a0` dest, `a1` src, `a2` size for memcpy), and no side effects the replacement wouldn't have. A
   replacement that doesn't match the original is a much worse bug than the one it fixes.

Steps 2 and 3 as one script (the SSX run, so 12s in and a pixel at 240,136 of each 8888 stride-512
framebuffer). Parameters take hex as well as decimal; replies are always decimal:

```
cpu.runUntilTime us=12000000
memory.breakpoint.add address=0x040443c0 size=4 write=true enabled=true
memory.breakpoint.add address=0x040cc3c0 size=4 write=true enabled=true
cpu.resume
hle.backtrace
:quit
```

```bash
Tools/wsdbg/target/release/wsdbg.exe --sync --compact --quiet --launch \
    Windows/x64/Release/PPSSPPHeadless.exe GAME.iso --memstick=memstick --cpu=interpreter --graphics=vulkan \
    --timeout-wall=240 < script.txt 2>/dev/null | grep -v '^<- log'
```

The first `hle.backtrace` frame is the writer: `entry` is the function's start, `code` the store that hit.
Follow up with `memory.disasm address=<entry> count=64` and `hle.func.hash address=<entry>` in the same
session. Once a replacement is in place, the same script shows it working: the hit's `source` reads
`GPUMemcpy/...`, its `pc` is the function's entry, and the backtrace starts at the caller.

Getting the hash: `hle.func.hash address=<anywhere in the function>` returns what the table entry needs:

```
<- hle.func.hash {"address":145310976,"hash":"3966eef670a93781","known":null,"size":188}
```

`known` names the entry that already matches, if any (it read `memcpy` once SSX's was added). The hash is the
one `HashFunctions()` in `MIPSAnalyst.cpp` takes when the code is scanned (CityHash64 over the
instructions with immediates masked out), so it doesn't depend on where the function was loaded.

Add the entry to `hardcodedHashes` in hash order, with a comment naming the game. Then check that it took
effect: with `--log --loglevel=4` the loader prints `Replaced memcpy at 08a94500 with hash 3966eef670a93781`.
Check other regions of the game too. Since the hash doesn't depend on address, they usually match the same
entry (SSX's Korean release has the function at `08a94200`).

