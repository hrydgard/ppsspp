// wsdbg - a tiny interactive client for the PPSSPP WebSocket debugger interface.
//
// See docs/WebSocketDebugger.md in the root of the ppsspp repo for the protocol reference.
//
// Examples:
//   wsdbg 12345                        # interactive REPL
//   wsdbg 12345 game.status            # one-shot: send an event, print responses, exit
//   wsdbg 12345 cpu.setReg thread=0 name=0 value=42

use std::collections::HashMap;
use std::io::{self, BufRead, Write};
use std::net::TcpStream;
use std::sync::atomic::{AtomicBool, AtomicU64, Ordering};
use std::sync::mpsc;
use std::thread;
use std::time::{Duration, Instant};

use anyhow::{Context, Result, anyhow};
use base64::Engine as _;
use clap::Parser;
use tungstenite::client::IntoClientRequest;
use tungstenite::http::HeaderValue;
use tungstenite::{Message, WebSocket};

// name -> (address, as the user typed it - not reparsed, just echoed back for display; bytes).
type Snapshots = HashMap<String, (String, Vec<u8>)>;

const SUBPROTOCOL: &str = "debugger.ppsspp.org";

#[derive(Parser, Debug)]
#[command(
    name = "wsdbg",
    about = "Talk directly to the PPSSPP WebSocket debugger interface",
    long_about = "Talk directly to the PPSSPP WebSocket debugger interface.\n\
                  Run with just a port to get an interactive REPL, or add an event name\n\
                  (and optional key=value parameters) to fire a single request and exit.\n\
                  See docs/WebSocketDebugger.md for the event catalog and protocol details."
)]
struct Args {
    /// Port the debugger is listening on (Settings > Tools > Developer Tools > Remote Debugger,
    /// or whatever port PPSSPP printed / --debugger chose).
    port: u16,

    /// Event to send once and then exit, e.g. "game.status" or "cpu.stepping".
    /// Omit this to start an interactive REPL instead.
    event: Option<String>,

    /// Extra parameters for the one-shot event, as key=value pairs.
    /// Values are parsed as JSON when possible (numbers, true/false, "quoted strings"),
    /// otherwise sent as a plain string.
    params: Vec<String>,

    /// Host to connect to.
    #[arg(long, default_value = "127.0.0.1")]
    host: String,

    /// How long to wait for responses/broadcasts in one-shot mode, in seconds.
    #[arg(long, default_value_t = 2.0)]
    wait: f64,

    /// Send this raw JSON text instead of building one from event/params (one-shot mode).
    #[arg(long)]
    raw: Option<String>,

    /// REPL mode only: after sending a command, block until its ticketed response arrives
    /// (and, for cpu.resume/cpu.step*/cpu.runUntil, until the following cpu.stepping broadcast
    /// too) before reading the next input line. Turns a scripted sequence like
    /// "(echo cmd1; sleep 1; echo cmd2; ...)" into "(echo cmd1; echo cmd2; ...)" - no more
    /// guessing how long each step takes. All messages still print as they arrive; this only
    /// changes when the *next* line gets read. Has no effect on one-shot mode (event/--raw),
    /// which already waits up to --wait seconds for everything regardless.
    #[arg(long)]
    sync: bool,

    /// With --sync, how long to wait for a command's response (and, for resume/step commands,
    /// the following cpu.stepping) before giving up and reading the next line anyway, in
    /// seconds. A breakpoint that never trips (bad condition, wrong address, etc.) would
    /// otherwise hang the script forever.
    #[arg(long, default_value_t = 30.0)]
    sync_timeout: f64,

    /// Print one line per message instead of pretty-printed JSON, and drop the prompt and
    /// banner. Intended for piped scripts, where the multi-line default output has to be
    /// reassembled by whatever is reading it.
    #[arg(long)]
    compact: bool,
}

// Set from --compact. A global because print_incoming is called from a dozen places that have no
// business threading a formatting flag through.
static COMPACT: AtomicBool = AtomicBool::new(false);

fn compact() -> bool {
    COMPACT.load(Ordering::Relaxed)
}

// The "> " prompt is noise in a piped script, and interleaves with incoming messages.
fn print_prompt() {
    if !compact() {
        print!("> ");
        io::stdout().flush().ok();
    }
}

// Events that mean "let the CPU run" - after their own ticketed response comes back, --sync
// also waits for the *next* cpu.stepping broadcast (which has no ticket of its own), since
// that's the actual "it stopped again" signal any script issuing one of these actually wants.
const RESUME_FAMILY: &[&str] = &[
    "cpu.resume",
    "cpu.stepInto",
    "cpu.stepOver",
    "cpu.stepOut",
    "cpu.runUntil",
    "cpu.nextHLE",
];

static TICKET: AtomicU64 = AtomicU64::new(1);

fn next_ticket() -> u64 {
    TICKET.fetch_add(1, Ordering::Relaxed)
}

// A minimal shell-like tokenizer: splits on unquoted whitespace, and lets 'single' or "double"
// quotes protect spaces (and each other) from being treated as a separator - e.g.
// `logFormat="job func v0={v0} a0={a0}"` becomes one token, not four bogus ones that
// build_event_json would silently misparse as extra, malformed key=value pairs (or fail on
// outright). Quote characters are stripped from the resulting token, same as a normal shell; a
// backslash escapes the very next character (including inside quotes), which is enough for this
// tool's purposes without pulling in a full shell-parsing crate.
fn split_shell_words(line: &str) -> Result<Vec<String>> {
    let mut words = Vec::new();
    let mut current = String::new();
    let mut in_word = false;
    let mut quote: Option<char> = None;
    let mut chars = line.chars().peekable();

    while let Some(c) = chars.next() {
        match quote {
            Some(q) => {
                if c == '\\' && chars.peek().is_some() {
                    current.push(chars.next().unwrap());
                } else if c == q {
                    quote = None;
                } else {
                    current.push(c);
                }
            }
            None => {
                if c == '\'' || c == '"' {
                    quote = Some(c);
                    in_word = true;
                } else if c == '\\' && chars.peek().is_some() {
                    current.push(chars.next().unwrap());
                    in_word = true;
                } else if c.is_whitespace() {
                    if in_word {
                        words.push(std::mem::take(&mut current));
                        in_word = false;
                    }
                } else {
                    current.push(c);
                    in_word = true;
                }
            }
        }
    }

    if quote.is_some() {
        return Err(anyhow!("Unterminated quote in command line"));
    }
    if in_word {
        words.push(current);
    }

    Ok(words)
}

fn parse_value(raw: &str) -> serde_json::Value {
    // Accept numbers, true/false, null, and "quoted strings" as JSON; anything
    // else (including bare words and unescaped strings) is sent as a plain string.
    serde_json::from_str::<serde_json::Value>(raw).unwrap_or_else(|_| serde_json::Value::String(raw.to_string()))
}

fn build_event_json(event: &str, params: &[String], ticket: Option<u64>) -> Result<String> {
    let mut map = serde_json::Map::new();
    map.insert("event".to_string(), serde_json::Value::String(event.to_string()));
    if let Some(t) = ticket {
        map.insert("ticket".to_string(), serde_json::Value::from(t));
    }
    for p in params {
        let (k, v) = p
            .split_once('=')
            .ok_or_else(|| anyhow!("Parameter '{p}' is not in key=value form"))?;
        map.insert(k.to_string(), parse_value(v));
    }
    Ok(serde_json::Value::Object(map).to_string())
}

fn connect(host: &str, port: u16) -> Result<WebSocket<TcpStream>> {
    let addr = format!("{host}:{port}");
    let stream = TcpStream::connect(&addr).with_context(|| format!("Failed to connect to {addr}"))?;
    stream.set_nodelay(true).ok();

    let url = format!("ws://{addr}/debugger");
    let mut request = url
        .into_client_request()
        .with_context(|| format!("Invalid URL: ws://{addr}/debugger"))?;
    request
        .headers_mut()
        .insert("Sec-WebSocket-Protocol", HeaderValue::from_static(SUBPROTOCOL));

    let (socket, _response) = tungstenite::client(request, stream)
        .map_err(|e| anyhow!("WebSocket handshake with {addr} failed: {e}"))?;
    Ok(socket)
}

fn print_incoming(text: &str) {
    let parsed = serde_json::from_str::<serde_json::Value>(text);
    if compact() {
        // One line per message, event name first so a script can grep for it without having to
        // reassemble pretty-printed JSON spread over twenty lines.
        match parsed {
            Ok(v) => {
                let event = v.get("event").and_then(|e| e.as_str()).unwrap_or("?");
                println!("<- {event} {}", serde_json::to_string(&v).unwrap_or_else(|_| text.to_string()));
            }
            Err(_) => println!("<- {text}"),
        }
        io::stdout().flush().ok();
        return;
    }
    match parsed {
        Ok(v) => println!("\n<- {}", serde_json::to_string_pretty(&v).unwrap_or_else(|_| text.to_string())),
        Err(_) => println!("\n<- {text}"),
    }
}

// Reads and prints messages until `done` says we're finished or the deadline passes. The one
// place that actually touches the socket while waiting, so a script never stops draining it -
// broadcasts keep printing during a :sleep, and the connection doesn't back up.
// Returns true if `done` was satisfied.
fn pump_until(
    socket: &mut WebSocket<TcpStream>,
    deadline: Instant,
    mut done: impl FnMut(&serde_json::Value) -> bool,
) -> bool {
    while Instant::now() < deadline {
        match socket.read() {
            Ok(Message::Text(text)) => {
                print_incoming(&text);
                if let Ok(v) = serde_json::from_str::<serde_json::Value>(&text) {
                    if done(&v) {
                        return true;
                    }
                }
            }
            Ok(Message::Close(frame)) => {
                println!("[connection closed by PPSSPP: {frame:?}]");
                return false;
            }
            Ok(_) => {}
            Err(ref e) if is_would_block(e) => {}
            Err(tungstenite::Error::ConnectionClosed | tungstenite::Error::AlreadyClosed) => {
                println!("[connection closed]");
                return false;
            }
            Err(e) => {
                eprintln!("[connection error: {e}]");
                return false;
            }
        }
    }
    false
}

// :sleep <seconds> - wall-clock pause that keeps draining the socket. Scripts needed this often
// enough that doing it by splitting them across several wsdbg invocations (one process, one
// connection and one handshake per pause) was the main reason driving headless was slow.
fn cmd_sleep(socket: &mut WebSocket<TcpStream>, args: &[&str]) {
    let secs = match args.first().map(|s| s.parse::<f64>()) {
        Some(Ok(s)) if s >= 0.0 => s,
        _ => {
            eprintln!("! Usage: :sleep <seconds>");
            return;
        }
    };
    pump_until(socket, Instant::now() + Duration::from_secs_f64(secs), |_| false);
}

// :wait <event> [timeout] - block until a message with that event name arrives. The precise
// version of :sleep: waiting for cpu.stepping after a resume beats guessing how long the game
// needs. Exits non-zero on timeout so a script can tell it didn't happen.
fn cmd_wait(socket: &mut WebSocket<TcpStream>, args: &[&str], default_timeout: f64) -> bool {
    let Some(want) = args.first() else {
        eprintln!("! Usage: :wait <event> [timeout]");
        return false;
    };
    let timeout = match args.get(1).map(|s| s.parse::<f64>()) {
        Some(Ok(t)) => t,
        Some(Err(_)) => {
            eprintln!("! :wait timeout must be a number");
            return false;
        }
        None => default_timeout,
    };
    let deadline = Instant::now() + Duration::from_secs_f64(timeout.max(0.0));
    let got = pump_until(socket, deadline, |v| {
        v.get("event").and_then(|e| e.as_str()) == Some(*want)
    });
    if !got {
        eprintln!("! :wait timed out after {timeout}s waiting for '{want}'");
    }
    got
}

fn is_would_block(e: &tungstenite::Error) -> bool {
    matches!(
        e,
        tungstenite::Error::Io(io_err)
            if io_err.kind() == io::ErrorKind::WouldBlock || io_err.kind() == io::ErrorKind::TimedOut
    )
}

fn run_one_shot(mut socket: WebSocket<TcpStream>, json_text: String, wait_secs: f64) -> Result<()> {
    println!("-> {json_text}");
    socket.send(Message::Text(json_text.into()))?;

    socket.get_ref().set_read_timeout(Some(Duration::from_millis(50)))?;
    let deadline = Instant::now() + Duration::from_secs_f64(wait_secs.max(0.0));
    while Instant::now() < deadline {
        match socket.read() {
            Ok(Message::Text(text)) => print_incoming(&text),
            Ok(Message::Close(frame)) => {
                println!("[connection closed: {frame:?}]");
                break;
            }
            Ok(_) => {}
            Err(ref e) if is_would_block(e) => {}
            Err(tungstenite::Error::ConnectionClosed | tungstenite::Error::AlreadyClosed) => break,
            Err(e) => {
                eprintln!("[connection error: {e}]");
                break;
            }
        }
    }
    Ok(())
}

fn print_help() {
    println!("wsdbg - connected. Type an event name and optional key=value params, e.g.:");
    println!("    game.status");
    println!("    cpu.setReg thread=0 name=4 value=1000");
    println!("Or paste a full JSON message starting with '{{' to send it verbatim.");
    println!("A numeric 'ticket' is auto-assigned to shorthand commands so you can match up responses.");
    println!(":help                              show this message");
    println!(":quit / :q                         disconnect and exit");
    println!(":snapshot <name> <addr> <size>     memory.read into a locally-named byte buffer");
    println!(":snapshots                         list saved snapshots");
    println!(":diff <name1> <name2>              byte-compare two snapshots");
    println!(":sleep <seconds>                   pause, still printing anything that arrives");
    println!(":wait <event> [timeout]            block until that event arrives (e.g. cpu.stepping)");
    println!(":echo <text>                       print text, for marking up a script's output");
    println!("# ...                              comment line, ignored");
    println!("See docs/WebSocketDebugger.md in the ppsspp repo for the full event catalog.");
    println!("Piping a script in? Pass --sync so each line waits for its response (and, for");
    println!("cpu.resume/step*/runUntil, the following cpu.stepping) before the next line runs -");
    println!("no more guessing sleep durations.");
}

// Returns the (ticket, event name) sent, when known - used by --sync to know what response to
// wait for. Both are recoverable for a plain "{...}" JSON paste too, as long as it includes
// "ticket"/"event" fields itself; if it doesn't (or ticket isn't a plain integer), --sync has
// nothing to wait on and just proceeds to the next line immediately, same as without --sync.
fn handle_repl_line(socket: &mut WebSocket<TcpStream>, line: &str) -> Result<Option<(u64, String)>> {
    if line.starts_with('{') {
        println!("-> {line}");
        socket.send(Message::Text(line.to_string().into()))?;
        let parsed: serde_json::Value = serde_json::from_str(line).unwrap_or(serde_json::Value::Null);
        let ticket = parsed.get("ticket").and_then(|t| t.as_u64());
        let event = parsed.get("event").and_then(|e| e.as_str()).map(|s| s.to_string());
        return Ok(ticket.zip(event));
    }

    let mut parts = split_shell_words(line)?.into_iter();
    let event = parts.next().ok_or_else(|| anyhow!("Empty command"))?;
    let rest: Vec<String> = parts.collect();
    let ticket = next_ticket();
    let json_text = build_event_json(&event, &rest, Some(ticket))?;
    println!("-> (ticket {ticket}) {json_text}");
    socket.send(Message::Text(json_text.into()))?;
    Ok(Some((ticket, event)))
}

// --sync support: after sending a command, read (and print, same as the normal loop) incoming
// messages until we've seen what that command actually completes with, or sync_timeout runs
// out. Returns to the normal loop either way; a timeout just means the next line gets read
// without having waited further.
//
// RESUME_FAMILY events (cpu.resume/step*/runUntil/nextHLE) are documented as having "no
// immediate response" at all - their handlers never call req.Respond(), only req.Fail() on
// error, so on success the *only* message that ever comes back is the unticketed cpu.stepping
// broadcast once the CPU actually stops again. Waiting for a ticketed ack for these would hang
// until the timeout every time, so don't - wait for cpu.stepping instead. Everything else uses
// the normal ticketed request/response pair.
fn wait_for_sync_response(socket: &mut WebSocket<TcpStream>, ticket: u64, event: &str, timeout_secs: f64) {
    let wants_stepping = RESUME_FAMILY.contains(&event);
    // wants_stepping: there's no ticketed response to wait for at all (see doc comment above),
    // so treat "got_ticket" as trivially satisfied and only really wait on got_stepping. The
    // reverse for every other event: no cpu.stepping is expected, only the ticketed response.
    let mut got_ticket = wants_stepping;
    let mut got_stepping = !wants_stepping;
    let deadline = Instant::now() + Duration::from_secs_f64(timeout_secs.max(0.0));

    while Instant::now() < deadline && !(got_ticket && got_stepping) {
        match socket.read() {
            Ok(Message::Text(text)) => {
                print_incoming(&text);
                if let Ok(v) = serde_json::from_str::<serde_json::Value>(&text) {
                    if !wants_stepping && v.get("ticket").and_then(|t| t.as_u64()) == Some(ticket) {
                        got_ticket = true;
                    }
                    if wants_stepping && v.get("event").and_then(|e| e.as_str()) == Some("cpu.stepping") {
                        got_stepping = true;
                    }
                }
            }
            Ok(Message::Close(frame)) => {
                println!("\n[connection closed by PPSSPP: {frame:?}]");
                return;
            }
            Ok(_) => {}
            Err(ref e) if is_would_block(e) => {}
            Err(tungstenite::Error::ConnectionClosed | tungstenite::Error::AlreadyClosed) => {
                println!("\n[connection closed]");
                return;
            }
            Err(e) => {
                eprintln!("\n[connection error: {e}]");
                return;
            }
        }
    }
    if !(got_ticket && got_stepping) {
        eprintln!("! --sync: timed out after {timeout_secs}s waiting for a response, continuing anyway");
    }
}

// Send a ticketed request and block until its response arrives (or timeout), returning the
// parsed JSON. Unlike the normal REPL dispatch (fire-and-forget, or --sync's "wait but don't
// look at the payload"), :snapshot needs the actual returned data before it can do anything -
// there's no useful way to "move on to the next input line" first.
fn send_and_wait(
    socket: &mut WebSocket<TcpStream>,
    event: &str,
    params: &[String],
    timeout_secs: f64,
) -> Result<serde_json::Value> {
    let ticket = next_ticket();
    let json_text = build_event_json(event, params, Some(ticket))?;
    println!("-> (ticket {ticket}) {json_text}");
    socket.send(Message::Text(json_text.into()))?;

    let deadline = Instant::now() + Duration::from_secs_f64(timeout_secs.max(0.0));
    while Instant::now() < deadline {
        match socket.read() {
            Ok(Message::Text(text)) => {
                print_incoming(&text);
                if let Ok(v) = serde_json::from_str::<serde_json::Value>(&text) {
                    if v.get("ticket").and_then(|t| t.as_u64()) == Some(ticket) {
                        return Ok(v);
                    }
                }
            }
            Ok(Message::Close(frame)) => return Err(anyhow!("connection closed by PPSSPP: {frame:?}")),
            Ok(_) => {}
            Err(ref e) if is_would_block(e) => {}
            Err(tungstenite::Error::ConnectionClosed | tungstenite::Error::AlreadyClosed) => {
                return Err(anyhow!("connection closed"));
            }
            Err(e) => return Err(anyhow!("connection error: {e}")),
        }
    }
    Err(anyhow!("timed out after {timeout_secs}s waiting for a response"))
}

// :snapshot <name> <address> <size> - reads memory.read once (blocking, unlike the rest of the
// REPL) and stores the decoded bytes locally under <name>. address/size are passed through to
// the server exactly as typed (same as any other event param - the server already accepts
// "0x..." hex strings for address-like fields), not reparsed here.
//
// Kept entirely client-side rather than as a new PPSSPP-side memory.snapshot.* event: a
// snapshot is a debugging-*session*-scoped concept (how long should the emulator hold onto one?
// does it survive a savestate load or game restart? does it leak if a script forgets to clean
// up?) with no real emulator-side meaning, and memory.read's existing base64 response is already
// the only primitive actually needed - no new protocol surface required.
fn cmd_snapshot(socket: &mut WebSocket<TcpStream>, snapshots: &mut Snapshots, args: &[&str], timeout_secs: f64) {
    if args.len() != 3 {
        eprintln!("! Usage: :snapshot <name> <address> <size>");
        return;
    }
    let name = args[0];
    let params = vec![format!("address={}", args[1]), format!("size={}", args[2])];
    match send_and_wait(socket, "memory.read", &params, timeout_secs) {
        Ok(resp) => {
            if resp.get("event").and_then(|e| e.as_str()) == Some("error") {
                let msg = resp.get("message").and_then(|m| m.as_str()).unwrap_or("unknown error");
                eprintln!("! memory.read failed: {msg}");
                return;
            }
            let b64 = match resp.get("base64").and_then(|b| b.as_str()) {
                Some(b) => b,
                None => {
                    eprintln!("! Response had no 'base64' field: {resp}");
                    return;
                }
            };
            match base64::engine::general_purpose::STANDARD.decode(b64) {
                Ok(bytes) => {
                    let len = bytes.len();
                    snapshots.insert(name.to_string(), (args[1].to_string(), bytes));
                    println!("snapshot '{name}' saved: {len} bytes at {}", args[1]);
                }
                Err(e) => eprintln!("! Could not decode base64 response: {e}"),
            }
        }
        Err(e) => eprintln!("! {e}"),
    }
}

// :snapshots - list what's been captured so far in this session.
fn cmd_list_snapshots(snapshots: &Snapshots) {
    if snapshots.is_empty() {
        println!("No snapshots saved. Use :snapshot <name> <address> <size> to take one.");
        return;
    }
    let mut names: Vec<&String> = snapshots.keys().collect();
    names.sort();
    for name in names {
        let (addr, bytes) = &snapshots[name];
        println!("  {name}: {} bytes at {addr}", bytes.len());
    }
}

// :diff <name1> <name2> - byte-compare two snapshots and print each differing run as
// "+offset (N bytes): old_hex -> new_hex". Replaces the throwaway PowerShell/Bash diffing this
// was needed for by hand at least twice during the VSH boot investigation (see
// docs/VSHBootInvestigation.md) with one correct implementation.
fn cmd_diff(snapshots: &Snapshots, args: &[&str]) {
    if args.len() != 2 {
        eprintln!("! Usage: :diff <name1> <name2>");
        return;
    }
    let Some((addr1, bytes1)) = snapshots.get(args[0]) else {
        eprintln!("! No snapshot named '{}' (see :snapshots)", args[0]);
        return;
    };
    let Some((addr2, bytes2)) = snapshots.get(args[1]) else {
        eprintln!("! No snapshot named '{}' (see :snapshots)", args[1]);
        return;
    };

    let len = bytes1.len().min(bytes2.len());
    if bytes1.len() != bytes2.len() {
        println!(
            "'{}' ({} bytes at {addr1}) and '{}' ({} bytes at {addr2}) differ in size - comparing the first {len} bytes",
            args[0], bytes1.len(), args[1], bytes2.len()
        );
    }

    const MAX_RUNS: usize = 50;
    let mut runs = 0usize;
    let mut i = 0usize;
    while i < len {
        if bytes1[i] == bytes2[i] {
            i += 1;
            continue;
        }
        let start = i;
        while i < len && bytes1[i] != bytes2[i] {
            i += 1;
        }
        runs += 1;
        if runs <= MAX_RUNS {
            let old_hex: Vec<String> = bytes1[start..i].iter().map(|b| format!("{b:02x}")).collect();
            let new_hex: Vec<String> = bytes2[start..i].iter().map(|b| format!("{b:02x}")).collect();
            let count = i - start;
            println!(
                "  +{start:#06x} ({count} byte{}): {} -> {}",
                if count == 1 { "" } else { "s" },
                old_hex.join(" "),
                new_hex.join(" ")
            );
        }
    }
    if runs == 0 {
        println!("'{}' and '{}' are identical (first {len} bytes)", args[0], args[1]);
    } else if runs > MAX_RUNS {
        println!("  ... and {} more differing run(s)", runs - MAX_RUNS);
    }
}

// Returns false if anything in the script failed (a :wait that timed out, say), so a piped run
// can be checked with an exit code instead of by grepping its output.
fn run_repl(mut socket: WebSocket<TcpStream>, sync: bool, sync_timeout: f64) -> Result<bool> {
    socket.get_ref().set_read_timeout(Some(Duration::from_millis(100)))?;

    // Politely say hello, per protocol convention (see WebSocket/GameSubscriber.cpp).
    let hello = build_event_json(
        "version",
        &[
            "name=wsdbg".to_string(),
            format!("version={}", env!("CARGO_PKG_VERSION")),
        ],
        Some(next_ticket()),
    )?;
    socket.send(Message::Text(hello.into()))?;

    if !compact() {
        print_help();
    }

    let (tx, rx) = mpsc::channel::<String>();
    thread::spawn(move || {
        let stdin = io::stdin();
        for line in stdin.lock().lines() {
            match line {
                Ok(l) => {
                    if tx.send(l).is_err() {
                        break;
                    }
                }
                Err(_) => break,
            }
        }
    });

    print_prompt();

    let mut snapshots: Snapshots = Snapshots::new();
    let mut failed = false;

    loop {
        match rx.try_recv() {
            Ok(line) => {
                let line = line.trim();
                let mut words = line.split_whitespace();
                match words.next() {
                    Some(":quit") | Some(":q") | Some(":exit") => return Ok(!failed),
                    Some(":help") | Some(":h") => print_help(),
                    Some(":snapshot") => {
                        let args: Vec<&str> = words.collect();
                        cmd_snapshot(&mut socket, &mut snapshots, &args, sync_timeout);
                    }
                    Some(":snapshots") => cmd_list_snapshots(&snapshots),
                    Some(":diff") => {
                        let args: Vec<&str> = words.collect();
                        cmd_diff(&snapshots, &args);
                    }
                    Some(":sleep") => {
                        let args: Vec<&str> = words.collect();
                        cmd_sleep(&mut socket, &args);
                    }
                    Some(":wait") => {
                        let args: Vec<&str> = words.collect();
                        if !cmd_wait(&mut socket, &args, sync_timeout) {
                            failed = true;
                        }
                    }
                    Some(":echo") => println!("{}", words.collect::<Vec<&str>>().join(" ")),
                    Some("#") => {}
                    Some(w) if w.starts_with('#') => {}
                    None => {}
                    _ => match handle_repl_line(&mut socket, line) {
                        Err(e) => eprintln!("! {e}"),
                        Ok(Some((ticket, event))) if sync => {
                            wait_for_sync_response(&mut socket, ticket, &event, sync_timeout);
                        }
                        Ok(_) => {}
                    },
                }
                print_prompt();
            }
            Err(mpsc::TryRecvError::Disconnected) => return Ok(!failed),
            Err(mpsc::TryRecvError::Empty) => {}
        }

        match socket.read() {
            Ok(Message::Text(text)) => {
                print_incoming(&text);
                print_prompt();
            }
            Ok(Message::Close(frame)) => {
                println!("\n[connection closed by PPSSPP: {frame:?}]");
                return Ok(!failed);
            }
            Ok(_) => {}
            Err(ref e) if is_would_block(e) => {}
            Err(tungstenite::Error::ConnectionClosed | tungstenite::Error::AlreadyClosed) => {
                println!("\n[connection closed]");
                return Ok(!failed);
            }
            Err(e) => {
                eprintln!("\n[connection error: {e}]");
                return Ok(false);
            }
        }
    }
}

fn main() -> Result<()> {
    let args = Args::parse();
    COMPACT.store(args.compact, Ordering::Relaxed);

    let socket = connect(&args.host, args.port).with_context(|| {
        "Could not connect. Is PPSSPP running with the WebSocket debugger enabled? \
         (Settings > Tools > Developer Tools > Allow remote debugger, or launch with --debugger)"
    })?;

    if let Some(raw) = &args.raw {
        return run_one_shot(socket, raw.clone(), args.wait);
    }

    if let Some(event) = &args.event {
        let ticket = next_ticket();
        let json_text = build_event_json(event, &args.params, Some(ticket))?;
        return run_one_shot(socket, json_text, args.wait);
    }

    // Non-zero exit when something in the script failed, so a caller doesn't have to grep output
    // to find out whether its :wait ever fired.
    if run_repl(socket, args.sync, args.sync_timeout)? {
        Ok(())
    } else {
        std::process::exit(1);
    }
}
