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
./Windows/x64/Debug/PPSSPPHeadless.exe -i --debugger=34567 --timeout-wall=100000 --graphics=software --log \
    --root pspautotests/tests/../ pspautotests/tests/cpu/cpu_alu/cpu_alu.prx > hl.log 2>&1 &
# wait for "Listening on port" in hl.log, then:
./Tools/wsdbg/target/release/wsdbg.exe 34567 --sync --sync-timeout 15 < script.txt
```

- **The timeouts apply to the whole session**, not per test. `--timeout-wall=N` is real seconds and
  `--timeout-emulated=N` is emulated ones; both default to infinity, both can be set at once, and whichever
  is reached first ends the run (`TIMEOUT` or `TIMEOUT (emulated)`). `--timeout` is the old name for
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
  past as if all were well. Reproduce display bugs on `--graphics=d3d11` or `--graphics=vulkan` (`directx9` is not
  a valid value) and compare `--screenshot-save=` output, not the log.
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

**If headless goes silent early in a game's boot, check which GPU backend it's actually using.** From
551e4cd0ab (2026-08-04) until 2026-09-22, headless without `--graphics` quietly ran the OpenGL backend instead of
the software renderer the README promises (`bSoftwareRendering` defaulted to false). Under Mesa llvmpipe on
Linux/WSL, OpenGL hung every commercial game tried early in boot, e.g. AI Go right after its
`sceKernelCreateCallback`, with no CPU use. Neither `--timeout-wall` nor `--timeout-emulated` fired, because both
are checked only when the emulation loop comes back around, so a blocked host thread defeats them. `test.py`
passes no `--graphics` either, so it stalled the same way. The default is software again; the OpenGL hang itself
isn't fixed.

**For game runs, prefer `--graphics=vulkan`.** Headless renders it offscreen, into images of its own with no
window, surface or swapchain, so it needs no display, and works on macOS through MoltenVK. It's far faster than
the software renderer, which runs display lists synchronously inside `sceGeListEnQueue` and so dominates any
profile of the emulator thread: 30 emulated seconds of God of War take about 4 seconds instead of a minute. OpenGL
headless deadlocks at startup on macOS too, main thread in `GLRenderManager::ThreadFrame` with no CPU use. The
pspautotests pass on Vulkan apart from 17 GPU tests, whose references are hardware screenshots that the hardware
backends don't match exactly (edge pixels, dithering, filtering, Metal's always-on primitive restart), so keep
`--graphics=software` for those.

That cost a lot of time because the first theories were confounded. Runs "worked in the background and hung in
the foreground" only because the background ones happened to have `--graphics=software` added. Change one variable
at a time, and diff the full command lines of a good and a bad run before theorising about the environment. If a
run is still silent a few seconds in, it isn't going to recover, so don't wait out the full timeout. Other things
that made it worse:

- `docs/debugging.md` wasn't read first, so the `timeout`-wrapper and full-`--log` warnings above were missed.
- The runs were wrapped in `timeout`, and a tool-call limit shorter than that sent them to the background. The
  follow-up `pkill -f` then killed its own shell rather than the emulators, so hung instances piled up across runs.
- `--memstick` was blamed first. It matters for LLE firmware modules, but a game that needs none (AI Go) stalled
  just the same without it.
- Pass `--graphics=software` whenever rendering doesn't matter, even though it's the default, so a copied
  command line doesn't depend on the default.

For the silent-fallback half of this, headless refuses the run rather than substituting: an explicit
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

## Debugging and breakpoint considerations

It might be worth trying the interpreter - all types of breakpoints are the most reliable with this CPU backend.
The JITs are much, much faster and in theory also support breakpoints, and we're trying to make the JITs
as reliable, but are maybe not quite there.

Concretely, as measured against the headless build (2026-08-16), per CPU backend:

| | interpreter (`-i`) | JIT (`-j`) / IR JIT (`-J`) |
|---|---|---|
| `cpu.breakpoint.*` (exec) | works | works |
| `cpu.stepInto/Over/Out`, `runUntil`, `nextHLE` | works | works |
| `memory.breakpoint.*` (memchecks) | works | **only for constant addresses** |
| `cpu.regBreakpoint.*` | works | never trips (as documented) |

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

