# PPSSPP WebSocket Debugger

PPSSPP has a JSON/WebSocket-based debugger and automation API, served from the
same HTTP server used for "Remote ISO" disc sharing and file upload. It lets
an external tool (a script, a web page, another editor/IDE) inspect and
control a running emulation session: read/write memory, set breakpoints,
step the CPU, read GE/GPU state, send fake input, tail the log, etc.

This doc is a local reference, the user-facing documentation is on the website.

## Where the code lives

- `Core/WebServer.cpp` / `Core/WebServer.h` - the shared HTTP server (also
  used for Remote ISO and file upload). It owns the listening socket and
  dispatches `/debugger` requests.
- `Core/Debugger/WebSocket.cpp` - upgrades the HTTP request to a WebSocket and
  runs the per-connection event loop (`HandleDebuggerRequest`).
- `Core/Debugger/WebSocket/*.cpp/.h` - one "subscriber" or "broadcaster" per
  feature area (CPU, memory, GPU, HLE, input, breakpoints, ...). Each file's
  top comment documents its events in detail - this doc gives the overview
  and an index into those files.
- `Core/Debugger/WebSocket/WebSocketUtils.h` - shared `DebuggerRequest`
  helper (parameter parsing, response/error helpers) and `DebuggerSubscriber`
  base class.
- `Common/Net/WebsocketServer.h/.cpp` - the low-level WebSocket framing.

## Transport

- Runs on the same port as Remote ISO sharing (`g_Config.iRemoteISOPort`; `0`
  means "pick a free port automatically" - the actual bound port is written
  back to that config value and logged: `Listening on port N`).
- URL path: `/debugger`.
- WebSocket subprotocol: `debugger.ppsspp.org` (required - a plain HTTP GET
  to `/debugger` without a websocket Upgrade just redirects to the bundled
  web UI at `/debugger/index.html`).
- Messages are JSON, both directions, always shaped as `{"event": "NAME", ...}`.
- One WebSocket connection = one client; PPSSPP does not limit the number of
  simultaneous debugger connections.
- The debugger only actually does anything while `WebServerFlags::DEBUGGER`
  is enabled (see "Enabling it" below) - the HTTP server itself may also be
  running for other reasons (Remote ISO, upload).

## Message protocol

Requests you send:
```json
{ "event": "cpu.status" }
```
Optionally include a `"ticket"` field (any JSON value) - PPSSPP echoes it
back verbatim in the response/error, so you can correlate requests and
responses when firing several at once. `Tools/wsdbg` (see below) assigns an
incrementing integer ticket automatically.

Responses use the *same* event name as the request:
```json
{ "event": "cpu.status", "ticket": 1, ... }
```

**The ticket convention**: send one whenever you care about the answer. Since
a response reuses the request's event name, a ticket is the only thing that
distinguishes *your* answer from an unsolicited broadcast of the same name, or
from the answer to an identical request you sent a moment earlier. Conversely,
for a request that doesn't answer immediately (below), leaving the ticket off
says you aren't waiting for anything.

Responses are not always immediate. `cpu.resume`, `cpu.stepInto`,
`cpu.stepOver`, `cpu.stepOut`, `cpu.runUntil`, `cpu.runUntilTime`,
`cpu.nextHLE`, `cpu.stepping` and `gpu.stats.feed` send nothing back at the
time of the request; what follows later is the broadcast that reports the
actual outcome (`cpu.stepping` / `cpu.resume`), which carries no ticket.
`input.buttons.press` is the odd one out - it answers with the request's own
event name *and* ticket, but only once the button has been held for the
requested number of frames.

If you would rather not track which those are, ask to be told explicitly:

```json
-> { "event": "client.config.set", "acknowledgeDeferred": true }
-> { "event": "cpu.resume", "ticket": 7 }
<- { "event": "deferred", "for": "cpu.resume", "ticket": 7 }
<- { "event": "cpu.resume" }
```

With that on, every request draws exactly one immediate reply - a response, an
`error`, or a `deferred` - so a client can correlate without a hardcoded list,
including for events added in future versions. It is off by default and must
stay that way: an extra message would break a client that correlates purely by
ticket, and it can't reuse the request's event name because for
`input.buttons.press` that is exactly what the real, later answer looks like.

Errors look like this:
```json
{ "event": "error", "message": "...", "level": 2, "ticket": 1 }
```
`level` is a `LogLevel` (1=NOTICE, 2=ERROR, 3=WARN, 4=INFO, 5=DEBUG, 6=VERBOSE).

PPSSPP also sends unsolicited ("broadcast") events with no request - see
below.

By convention, send a `version` event right after connecting (see
`WebSocket/GameSubscriber.cpp`):
```json
{ "event": "version", "name": "my-tool", "version": "1.0" }
```
PPSSPP responds with its own name/version, and remembers yours (currently
just for internal bookkeeping/future logging). The response also carries
`pid` (OS process id) and `path` (the executable/disc currently loaded, or
`null`), so an automation client can confirm it attached to the instance it
meant to - a port alone doesn't prove that, since a leftover process may still
be holding the port you asked for.

## Broadcast (unsolicited) events

Sent without you asking, whenever the underlying state changes:

| Event | Sent when | Source |
|---|---|---|
| `log` | A new log line is emitted | `LogBroadcaster.cpp` |
| `game.start` | A game finishes booting | `GameBroadcaster.cpp` |
| `game.quit` | The game is closed/reset | `GameBroadcaster.cpp` |
| `game.pause` / `game.resume` | User opens/leaves the pause menu | `GameBroadcaster.cpp` |
| `cpu.stepping` | CPU enters a stepping/break state | `SteppingBroadcaster.cpp` |
| `cpu.resume` | CPU resumes from stepping | `SteppingBroadcaster.cpp` |
| `cpu.breakpoint.hit` | Any breakpoint trips, whether or not it stops the CPU | `WebSocket.cpp` |
| `input.buttons` | Any emulated button changes state | `InputBroadcaster.cpp` |
| `input.analog` | An analog stick position changes | `InputBroadcaster.cpp` |

A client can opt out of specific broadcast categories with
`broadcast.config.set` (`{"disallowed": {"logger": true, "game": true, "stepping": true, "input": true, "breakpoint": true}}`),
see `ClientConfigSubscriber.cpp`. `gpu.stats.feed` (see below) works the same
way for periodic GPU stats. `client.config.set` in the same file carries
per-connection settings that aren't about broadcasts - currently just
`acknowledgeDeferred`, described under "Message protocol" above.

### Source line info

Where a game shipped an unstripped ELF, PPSSPP decodes its DWARF `.debug_line`
and can map an address to a source file and line. `cpu.breakpoint.hit` and
`cpu.stepping` carry `file`/`line` in the `hit` object, and `hle.backtrace`
carries them per frame - which is where it pays off most:

```
08841f98  move sp,fp          mesh.zig:163
0883afa4  li v0,0x0           MenuState.zig:821
088260d8  andi at,v0,0xFFFF   State.zig:40
0882a27c  andi at,v0,0xFFFF   engine.zig:468
```

Both fields are `null` when there's no line info for that address, which is the
common case: **PRX conversion strips every `.debug` section**, so this never
applies to a commercial game. In practice it means homebrew that ships its
`app.elf` next to the EBOOT - the same file the companion symbol loader uses -
or a plain `.elf` you built yourself. It follows `bAutoSaveLoadSymbols` along
with the symbols.

DWARF 2, 3 and 4 are decoded; version 5 units are skipped with a log line rather
than mis-parsed, since it re-encoded the file table. Nothing targeting the PSP
emits it today (psp-gcc produces 2, Zig 4).

### Emulation speed

`game.speed.set` drives two independent things:

```json
-> { "event": "game.speed.set", "fastForward": true }        // unlimited
-> { "event": "game.speed.set", "percent": 200 }             // double speed
-> { "event": "game.speed.set", "percent": 25 }              // quarter speed
-> { "event": "game.speed.set", "percent": null }            // drop the override
<- { "event": "game.speed.set", "fastForward": false, "percent": 200, "limitFps": 120 }
```

Percentages are relative to 60 FPS, matching how the in-app settings present the
alternative speeds - so `200` means the same thing in both places. `percent`
must be at least 1; use `fastForward` for unlimited rather than `0`, so there's
only one way to say it. `fastForward` wins while it is on, and `percent` is
remembered underneath it.

`limitFps` in the response is the frame rate throttling is actually aiming for
once everything - fast-forward, this override, the user's own alternative-speed
hotkeys - has been taken into account, with `0` meaning unlimited. It comes
straight from the function the frame timing itself consumes, so prefer reading
it over inferring the result from the other two fields.

This is a **separate channel** from the user's own alternative speeds. It
deliberately never touches `g_Config`, whose speed settings are persisted per
game, so a debugger session can't permanently change what the user configured.
Clearing the override also only stands down from a limit set through this event,
never from one the user set themselves. It resets on every game boot.

The request **fails** rather than being silently ignored when something else
owns the speed: RetroAchievements hardcore mode, or being connected to a network
game without "allow speed control while connected".

Headless sets fast-forward at startup and never turns it off on its own, so
`game.speed.set` works there too - turning fast-forward off makes headless
throttle to real time, which is occasionally useful for a wall-clock repro.

### Breakpoint hits

`cpu.breakpoint.hit` fires every time a breakpoint's condition passes and it has
some action set - including **log-only breakpoints, which never stop the CPU**.
That's what makes them usable for automation: before this event existed, a
log-only breakpoint's only trace was a line in the log stream.

```json
{
  "event": "cpu.breakpoint.hit",
  "sequence": 1,
  "hit": {
    "kind": "exec",
    "pc": 142876568,
    "address": 142876568,
    "hits": 1,
    "logged": true,
    "paused": false,
    "condition": null,
    "symbol": "rendering.mesh.Mesh(rendering.Vertex.PspVertex).draw",
    "breakpoint": { "start": 142876568, "end": 142876568 }
  }
}
```

The same `hit` object is attached to `cpu.stepping` when a breakpoint is what
stopped the CPU, so both can be parsed the same way. It is **absent** when the
break came from something else (the user pausing, a savestate load, an
exception), so test for its presence rather than for a `kind`.

Fields common to every kind:

| Field | Meaning |
|---|---|
| `kind` | `"exec"`, `"memory"` or `"register"` |
| `pc` | The instruction responsible |
| `address` | Exec: the instruction. Memory: the address **actually accessed** |
| `hits` | Total times this breakpoint has tripped, matching `*.list` |
| `logged` / `paused` | Which actions it had - `paused` false means the CPU kept running |
| `condition` | The condition expression, or `null` |
| `symbol` | Symbol at `address`, or `null` - resolved here to save a round trip |
| `file` / `line` | Source location of `pc`, or `null`. See "Source line info" below |
| `breakpoint` | `{start, end}` identifying which breakpoint fired. Absent for `"register"`, whose identity is the register, not an address |

Extra fields for `"memory"`:

| Field | Meaning |
|---|---|
| `size` | Bytes accessed |
| `access` | `"read"` or `"write"` |
| `source` | Who performed it - `"interpret"`, `"CPU"`, `"HLE"`, or an allocation tag such as `"ThreadFillStack"` |

Extra fields for `"register"`: `register` (GPR index) and `registerName`
(e.g. `"a0"`).

Note `address` and `breakpoint.start` are **not** the same thing for a memory
breakpoint watching a range - the first is the byte touched, the second is the
range being watched. On `cpu.stepping` the legacy `relatedAddress` field keeps
reporting the range start; `hit.address` is the accurate one.

`sequence` counts hits *produced*, not delivered. A connection whose queue backs
up (easy to do with a log-only breakpoint in a hot loop - one can produce tens
of thousands of hits per second) drops events rather than growing without bound,
so a gap in `sequence` tells a client exactly how many it missed. Turning the
`breakpoint` category off via `broadcast.config.set` avoids the traffic
entirely.

## Request/response event catalog

Full details (parameters, response shape) are documented as comments above
each handler in the corresponding `Core/Debugger/WebSocket/*Subscriber.cpp`
file - this is just an index.

| Category | Events | File |
|---|---|---|
| Game/version | `game.reset`, `game.status`, `game.speed.get/set` (emulation speed - unlimited fast-forward, or a percentage of 60 FPS; see below), `version` | `GameSubscriber.cpp` |
| CPU core | `cpu.stepping`, `cpu.resume`, `cpu.status` (reports `ticks` plus `us`, emulated microseconds, and `clockHz` - use `us` to line up with wall-clock timings, since games change the clock frequency and the ticks-per-second ratio isn't fixed), `cpu.getAllRegs`, `cpu.getReg`, `cpu.setReg`, `cpu.evaluate` | `CPUCoreSubscriber.cpp` |
| Stepping | `cpu.stepInto`, `cpu.stepOver`, `cpu.stepOut`, `cpu.runUntil`, `cpu.runUntilTime` (run until a point in emulated time - `us` absolute or `relativeUs` from now - and break there; this is how to get a scripted repro reproducibly "N seconds into the game" instead of polling `cpu.status` in a loop), `cpu.nextHLE` | `SteppingSubscriber.cpp` |
| Breakpoints | `cpu.breakpoint.add/update/remove/list`, `memory.breakpoint.add/update/remove/list`, `cpu.regBreakpoint.add/update/remove/list` (break when a register is written to, by any instruction anywhere - currently GPRs only; interpreter-only, no effect under a JIT backend) | `BreakpointSubscriber.cpp` |
| Memory read/write | `memory.read_u8/u16/u32`, `memory.read`, `memory.readString`, `memory.write_u8/u16/u32`, `memory.write`. The numeric ones report the result as both `value` and `uintValue` - the latter is what `cpu.getReg`/`cpu.getAllRegs` call it, so a client can read either without caring which event answered | `MemorySubscriber.cpp` |
| Memory search | `memory.search` - scan a range for a `u8`/`u16`/`u32`/`float` value or a `bytes` pattern (with an optional wildcard mask), for narrowing down where an unknown value lives (Cheat Engine style) | `MemorySubscriber.cpp` |
| Memory info/annotations | `memory.mapping`, `memory.info.config/set/list/search` | `MemoryInfoSubscriber.cpp` |
| Disassembly | `memory.base`, `memory.disasm` (add `compact=true` for plain-text lines instead of full per-field objects), `memory.searchDisasm` (add `findAll=true` for every match instead of just the first - e.g. "every caller of this address"), `memory.assemble` | `DisasmSubscriber.cpp` |
| GE display list disassembly | `gpu.displaylist.disasm` - like `memory.disasm` but for GE command words (`CLEARMODE`, `PRIM`, etc.) instead of CPU instructions; also supports `compact=true` | `GPUDisasmSubscriber.cpp` |
| HLE | `hle.thread.list/wake/stop`, `hle.func.list/add/remove/removeRange/rename/scan`, `hle.module.list`, `hle.module.saveSymbols/loadSymbols` (save/load one module's symbols to/from its standard `PSP/SYSTEM/SYMBOLS/<moduleName>_<crc>.ppsym` file, shared across any game that loads the same module - see `SymbolMap::GetModuleSymbolsPath`), `hle.game.saveSymbols/loadSymbols` (the same for symbols that aren't inside any module - heap, stack, scratchpad, hardware registers - which describe one game's memory layout and so go to a per-game `PSP/SYSTEM/SYMBOLS/<gameID>_syms.ppsym` instead; see `SymbolMap::GetGameSymbolsPath`), `hle.backtrace` | `HLESubscriber.cpp` |
| Data symbols | `hle.data.list/add/remove/rename` - label discovered data (structs, tables, buffers) with a name/type, same idea as `hle.func.*` but for `ST_DATA` symbols | `HLESubscriber.cpp` |
| Kernel objects | `hle.object.list` (every live kernel object of every type at once, with an optional `type` filter - uid/type/name/one-line summary only); `hle.eventflag.list/info`, `hle.mutex.list/info`, `hle.semaphore.list/info`, `hle.msgpipe.list/info`, `hle.callback.list/info` (per-type full detail, including waiting-thread lists) - all read-only, never mutate kernel state | `HLEKernelObjectSubscriber.cpp` |
| GPU stats | `gpu.stats.get`, `gpu.stats.feed` | `GPUStatsSubscriber.cpp` |
| GPU recording | `gpu.record.dump` | `GPURecordSubscriber.cpp` |
| GPU buffers | `gpu.buffer.screenshot`, `gpu.buffer.renderColor/renderDepth/renderStencil`, `gpu.buffer.texture`, `gpu.buffer.clut` | `GPUBufferSubscriber.cpp` |
| Input injection | `input.buttons.send`, `input.buttons.press`, `input.analog.send` | `InputSubscriber.cpp` |
| Replay | `replay.begin/abort/flush/execute/status`, `replay.time.get/set` | `ReplaySubscriber.cpp` |
| Client config | `broadcast.config.get/set`, `client.config.get/set` | `ClientConfigSubscriber.cpp` |
| Log channels | `log.channels.list`, `log.channel.set` - query/change a log channel's level (string: `notice`/`error`/`warning`/`info`/`debug`/`verbose`) and/or enabled state; the `log` event itself (the passive message stream, unaffected by this) keeps its existing numeric `level`, see `LogBroadcaster.cpp` | `LogConfigSubscriber.cpp` |

## Enabling it

- **UI**: Settings > Tools > Developer Tools > "Allow remote debugger"
  checkbox (`UI/DeveloperToolsScreen.cpp`). The "Local Server Port" slider on
  the Networking screen sets the port (shared with Remote ISO sharing; `0` =
  auto-pick).
- **Config**: `RemoteDebuggerOnStartup=true` in `ppsspp.ini`
  (`g_Config.bRemoteDebuggerOnStartup`) starts it automatically on launch
  (`UI/NativeApp.cpp`).
- **Command line** (both application and headless builds): `--debugger=PORT`
  (`0` = pick a port automatically) - a shared auto-param in
  `Core/CmdLine.cpp`/`.h` (`CmdLineMode::Both`). `ApplyToConfig()` sets
  `iRemoteISOPort`/`bRemoteDebuggerOnStartup` for that run without persisting
  them to the config file.
  - A **non-zero** `PORT` is treated as mandatory (`WebServerSetRequireExactPort()`):
    if it can't be bound, the server does *not* silently fall back to some other
    free port the way the "Local Server Port" preference does, because a client
    was told to connect there. Headless exits non-zero; the application build
    logs an error and shows an OSD message but keeps running. Use `--debugger=0`
    and read the actual port from the `Listening on port N` log line if you'd
    rather not care which port you get.
  - On the **application** build this is exactly like ticking "Allow remote
    debugger" - the game boots and runs normally, debugger listening
    alongside it.
  - On the **headless** build (`headless/Headless.cpp`) it additionally
    forces `coreParameter.startBreak = true`, so the CPU halts before
    running anything - useful for setting breakpoints before launch.

## Discovery

For LAN auto-discovery (mainly useful for mobile), the server periodically
reports its `(local ip, port)` to `report.ppsspp.org/match/update` (see `RegisterServer()` in
`Core/WebServer.cpp`). Clients can query `report.ppsspp.org/match/list` to
get a list of candidate endpoints on the same network and try connecting to
each in turn.

## The bundled web-based JS debugger

`assets/debugger/` is a git submodule
(`https://github.com/unknownbrackets/ppsspp-debugger.git`, `bundled` branch -
see `.gitmodules`) containing a prebuilt React app. PPSSPP serves it directly
at `/debugger/` (`Core/WebServer.cpp`'s `HandleFallback`/`ServeAssetFile`),
so opening `http://<ip>:<port>/debugger/` in a browser gets you a full GUI
debugger for free. The actual editable source lives in a different branch of
that same repo (the `bundled` branch only holds the built output that gets
checked in here).

From reading the minified bundle (`assets/debugger/static/js/main.*.js`),
it connects like this:

- Manual connect: `new WebSocket("ws://ip:port/debugger", "debugger.ppsspp.org")`.
- Auto connect: `fetch("//report.ppsspp.org/match/list")` for a list of
  `{ip, port}` candidates (as registered by `RegisterServer()` above), then
  tries each with the same WebSocket call until one succeeds.

## Talking to it yourself

- `scripts/websocket-test.py` - old minimal Python one-shot script (needs the
  `websocket-client` pip package).
- `Tools/wsdbg/` - a small Rust CLI/REPL client for this session's work (see
  `Tools/wsdbg/README.md`): connects, does the `version` handshake, and lets
  you fire off events by hand or from a one-shot command line, printing
  responses and broadcasts as they arrive. Built and smoke-tested against a
  live PPSSPP instance while writing this doc.
