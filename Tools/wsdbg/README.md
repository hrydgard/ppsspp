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

One-shot mode - send a single event and exit after a short wait, handy for
scripting:

```bash
cargo run -- 12345 gpu.stats.get
cargo run -- 12345 cpu.setReg thread=0 name=4 value=1000
cargo run -- 12345 --raw '{"event":"cpu.evaluate","expression":"pc"}'
```

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

Matching is by ticket, always. A raw JSON line (the only way to send nested parameters) gets a
ticket assigned if it doesn't carry one, so it's waited for like any other line - previously it
had none, `--sync` had nothing to match, and it skipped waiting entirely, which let the next
line's response be read as this one's and quietly desynchronised the rest of the script. Raw lines
are also rejected up front, rather than sent and left to fail somewhere downstream, if they aren't
valid JSON, aren't an object, have no string `event`, or carry a `ticket` that isn't an integer.

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
# hand this to: wsdbg PORT --sync --compact < script.txt
{"event":"broadcast.config.set","disallowed":{"logger":true,"input":true}}
:echo === run to 1.5s of emulated time ===
cpu.runUntilTime us=1500000
:wait cpu.stepping 60
cpu.status
:echo === step twice ===
cpu.stepInto
cpu.stepInto
:quit
```

`cpu.runUntilTime` (see `docs/WebSocketDebugger.md`) is what makes that reproducible: it stops on
the requested emulated microsecond, so the same script reaches the same instruction every run.
Polling `cpu.status` in a loop instead lands somewhere different each time.
