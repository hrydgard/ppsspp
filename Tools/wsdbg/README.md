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
`cpu.runUntil`/`cpu.nextHLE` it also waits for the following `cpu.stepping` broadcast - the actual
"the CPU stopped again" signal those commands imply. Everything still prints as it arrives; this
only changes when the *next* line gets sent. A breakpoint that never trips would otherwise hang
the script forever, so it gives up after `--sync-timeout` seconds (default 30) and moves on.

```bash
(
  echo 'cpu.breakpoint.add address=134348800 enabled=true'
  echo 'cpu.resume'
  echo 'cpu.getAllRegs'
) | cargo run -- 12345 --sync
```
