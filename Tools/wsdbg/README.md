# wsdbg - WebSocket debugger CLI

A small standalone client for talking directly to PPSSPP's WebSocket debugger
interface (see `docs/WebSocketDebugger.md` in the root of the repo for the
full protocol reference and event catalog).

To install Rust and cargo, [go here](https://www.rust-lang.org/learn/get-started).

To run, with rust installed, change to this `Tools/wsdbg` directory, then:

```bash
cargo run -- <port>
```

(Or build once with `cargo build --release` and run the binary directly from
`target/release/`.)

## Usage

Start PPSSPP with the remote debugger enabled (`--debugger` on the command
line, or Settings > Tools > Developer Tools > Allow remote debugger) and note
the port it's listening on (shown in that same settings screen, and logged at
startup).

### Letting wsdbg start PPSSPP

For headless scripting, `--launch` removes all of that. It starts PPSSPP, learns the debugger port
from its output, connects once the socket accepts, and kills it again on the way out:

```bash
wsdbg --sync --compact --quiet --launch ./PPSSPPHeadless.exe --vsh -i --graphics=vulkan < script.txt
```

`--launch` takes the executable and all of its arguments, so it has to come last - everything after
it belongs to PPSSPP, and wsdbg's own flags go before it. `--debugger=0` is appended unless the
arguments already ask for a debugger port, so the OS picks a free one and nothing has to agree on a
port number in advance. PPSSPP's output is forwarded to wsdbg's stderr, separate from the protocol
messages on stdout, so `2>emu.log` keeps them apart.

This replaces the wrapper script the same job used to need: no launching in the background, no
polling a log file for the port (a race - you can attach to a *previous* run that still holds one),
no fixed `sleep` before connecting, and no leftover emulator processes, which `--timeout`'s
wall-clock budget otherwise leaves running for as long as it says.

Interactive REPL - type an event name and `key=value` params, get responses
and broadcasts (log messages, stepping notifications, etc.) printed as they
arrive:

```bash
cargo run -- 12345
> game.status
-> (ticket 2) {"event":"game.status","ticket":2}

<- {
  "event": "game.status",
  "game": null,
  "paused": false
}
```

One-shot mode - send a single event and exit as soon as its reply arrives, handy
for scripting:

```bash
cargo run -- 12345 gpu.stats.get
cargo run -- 12345 cpu.setReg thread=0 name=4 value=1000
cargo run -- 12345 --raw '{"event":"cpu.evaluate","expression":"pc"}'
```

The reply is matched by ticket, so this returns in milliseconds rather than
padding every invocation with a fixed sleep. `--wait` (default 10s) is only the
upper bound before it gives up and exits non-zero. Pass `--wait-all` to go back
to "print everything that arrives for `--wait` seconds", which is what you want
when watching broadcasts (log lines, `gpu.stats.feed`) rather than asking a
question.

Type `:help` in the REPL for a quick reminder, `:quit` to disconnect.

## Scripting a multi-step sequence

Piping several commands into the REPL (`(echo cmd1; echo cmd2; ...) | wsdbg PORT`) sends them all
immediately by default - nothing waits for a response before moving to the next line, so scripts
traditionally needed `sleep N` between commands to guess how long each one takes. Pass `--sync`
to remove the guessing: each line blocks until its own response arrives (matched by ticket) before
the next line is read, and for `cpu.resume`/`cpu.stepInto`/`cpu.stepOver`/`cpu.stepOut`/
`cpu.runUntil`/`cpu.runUntilTime`/`cpu.nextHLE` it also waits for the following `cpu.stepping`
broadcast - the actual "the CPU stopped again" signal those commands imply. Everything still
prints as it arrives; this only changes when the *next* line gets sent. A breakpoint that never
trips would otherwise hang the script forever, so it gives up after `--sync-timeout` seconds
(default 30), reports it, and makes the run exit non-zero.

Matching is by ticket, always - `--sync` never waits for "whatever message arrives next", which is
what used to quietly desynchronise a script. A raw JSON line (the only way to send nested
parameters) is sent exactly as written, so it's waited for only if *you* gave it a `ticket`;
without one there is nothing to match and `--sync` moves straight on to the next line. Raw lines
are rejected up front, rather than sent and left to fail somewhere downstream, if they aren't valid
JSON, aren't an object, have no string `event`, or carry a `ticket` that isn't an integer.

```bash
(
  echo 'cpu.breakpoint.add address=134348800 enabled=true'
  echo 'cpu.resume'
  echo 'cpu.getAllRegs'
) | cargo run -- 12345 --sync
```

### Script directives

A script often needs to wait for something that isn't a direct response to the line before it.
Doing that by splitting the script across several `wsdbg` invocations costs a process, a TCP
connection and a handshake per pause, which is slow enough to matter - a polling loop built that
way took minutes per run. These run inside the one session instead:

| Directive | What it does |
|---|---|
| `:sleep <seconds>` | Wall-clock pause. Keeps draining and printing messages while it waits. |
| `:wait <event> [timeout]` | Blocks until a message with that event name arrives. Exits non-zero if it never does. |
| `:echo <text>` | Prints text, for marking up a script's output. |
| `# comment` | Ignored. |

`--compact` prints one line per message (`<- event {json}`) instead of pretty-printed JSON, and
drops the banner and prompt - much easier for a shell to grep. wsdbg exits non-zero if a `:wait`
timed out, so a script can be checked without parsing its output at all.

A complete repro - boot, get several seconds in, step, inspect - in one file and one connection:

```
# hand this to: wsdbg PORT --sync --compact --quiet < script.txt
:echo === run to 1.5s of emulated time ===
cpu.runUntilTime us=1500000
cpu.status
:echo === step twice ===
cpu.stepInto
cpu.stepInto
:quit
```

**Don't add `:wait cpu.stepping` after `cpu.runUntilTime` there** (an earlier version of this
example did). Under `--sync` those commands already wait for the `cpu.stepping` that follows, so an
explicit `:wait` blocks for a *second* one that never arrives and burns the entire
`--sync-timeout`. It is a nasty failure to diagnose, because the emulator has already stopped
exactly where you asked: the run just sits there, and a script written with `--sync-timeout 400`
takes seven minutes instead of three seconds while looking like a slow boot. `:wait` is for events
nothing else is waiting on - a breakpoint hit during a free run, say.

For the same reason, never put `"stepping":true` in `broadcast.config.set`'s `disallowed`:
`cpu.stepping` is a broadcast with no ticket of its own, and muting it means nothing can ever
observe that a run finished. `--quiet` above is the safe way to get the same noise reduction - it
disables `logger` and `input` only, and wsdbg warns if a hand-written `broadcast.config.set` mutes
`stepping`.

`cpu.runUntilTime` (see `docs/WebSocketDebugger.md`) is what makes that reproducible: it stops on
the requested emulated microsecond, so the same script reaches the same instruction every run.
Polling `cpu.status` in a loop instead lands somewhere different each time.
