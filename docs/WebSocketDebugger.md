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
Responses are not always immediate - some handlers respond asynchronously.

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
just for internal bookkeeping/future logging).

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
| `input.buttons` | Any emulated button changes state | `InputBroadcaster.cpp` |
| `input.analog` | An analog stick position changes | `InputBroadcaster.cpp` |

A client can opt out of specific broadcast categories with
`broadcast.config.set` (`{"disallowed": {"logger": true, "game": true, "stepping": true, "input": true}}`),
see `ClientConfigSubscriber.cpp`. `gpu.stats.feed` (see below) works the same
way for periodic GPU stats.

## Request/response event catalog

Full details (parameters, response shape) are documented as comments above
each handler in the corresponding `Core/Debugger/WebSocket/*Subscriber.cpp`
file - this is just an index.

| Category | Events | File |
|---|---|---|
| Game/version | `game.reset`, `game.status`, `version` | `GameSubscriber.cpp` |
| CPU core | `cpu.stepping`, `cpu.resume`, `cpu.status`, `cpu.getAllRegs`, `cpu.getReg`, `cpu.setReg`, `cpu.evaluate` | `CPUCoreSubscriber.cpp` |
| Stepping | `cpu.stepInto`, `cpu.stepOver`, `cpu.stepOut`, `cpu.runUntil`, `cpu.nextHLE` | `SteppingSubscriber.cpp` |
| Breakpoints | `cpu.breakpoint.add/update/remove/list`, `memory.breakpoint.add/update/remove/list` | `BreakpointSubscriber.cpp` |
| Memory read/write | `memory.read_u8/u16/u32`, `memory.read`, `memory.readString`, `memory.write_u8/u16/u32`, `memory.write` | `MemorySubscriber.cpp` |
| Memory search | `memory.search` - scan a range for a `u8`/`u16`/`u32`/`float` value or a `bytes` pattern (with an optional wildcard mask), for narrowing down where an unknown value lives (Cheat Engine style) | `MemorySubscriber.cpp` |
| Memory info/annotations | `memory.mapping`, `memory.info.config/set/list/search` | `MemoryInfoSubscriber.cpp` |
| Disassembly | `memory.base`, `memory.disasm`, `memory.searchDisasm`, `memory.assemble` | `DisasmSubscriber.cpp` |
| HLE | `hle.thread.list/wake/stop`, `hle.func.list/add/remove/removeRange/rename/scan`, `hle.module.list`, `hle.backtrace` | `HLESubscriber.cpp` |
| Data symbols | `hle.data.list/add/remove/rename` - label discovered data (structs, tables, buffers) with a name/type, same idea as `hle.func.*` but for `ST_DATA` symbols | `HLESubscriber.cpp` |
| GPU stats | `gpu.stats.get`, `gpu.stats.feed` | `GPUStatsSubscriber.cpp` |
| GPU recording | `gpu.record.dump` | `GPURecordSubscriber.cpp` |
| GPU buffers | `gpu.buffer.screenshot`, `gpu.buffer.renderColor/renderDepth/renderStencil`, `gpu.buffer.texture`, `gpu.buffer.clut` | `GPUBufferSubscriber.cpp` |
| Input injection | `input.buttons.send`, `input.buttons.press`, `input.analog.send` | `InputSubscriber.cpp` |
| Replay | `replay.begin/abort/flush/execute/status`, `replay.time.get/set` | `ReplaySubscriber.cpp` |
| Client config | `broadcast.config.get/set` | `ClientConfigSubscriber.cpp` |

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
