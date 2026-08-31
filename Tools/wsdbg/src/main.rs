// wsdbg - a tiny interactive client for the PPSSPP WebSocket debugger interface.
//
// See docs/WebSocketDebugger.md in the root of the ppsspp repo for the protocol reference.
//
// Examples:
//   wsdbg 12345                        # interactive REPL
//   wsdbg 12345 game.status            # one-shot: send an event, print responses, exit
//   wsdbg 12345 cpu.setReg thread=0 name=0 value=42

use std::collections::{HashMap, HashSet};
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
    /// or whatever port PPSSPP printed / --debugger chose). Omit it when using --launch, which
    /// discovers the port itself.
    port: Option<u16>,

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

    /// One-shot mode: how long to wait for the response before giving up, in seconds. Normally
    /// it returns as soon as the reply for this request arrives (matched by ticket), so this is
    /// only an upper bound, not a fixed delay. Pass --wait-all to go back to "print everything
    /// that arrives for N seconds".
    #[arg(long, default_value_t = 10.0)]
    wait: f64,

    /// One-shot mode: keep reading for the whole --wait period instead of stopping at the
    /// response. Useful for watching broadcasts (log lines, gpu.stats.feed) rather than asking
    /// a question.
    #[arg(long)]
    wait_all: bool,

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

    /// Start PPSSPP ourselves instead of connecting to one that's already running, then use it
    /// for this session and kill it on the way out. Takes the executable and all of its
    /// arguments, so it has to come last: everything after it belongs to PPSSPP, not to wsdbg.
    ///
    ///   wsdbg --sync --compact --launch ./PPSSPPHeadless.exe --vsh -i --graphics=vulkan
    ///
    /// --debugger=0 is appended unless the given arguments already ask for a debugger port, and
    /// the port is read straight off the child's stdout rather than scraped from a log file.
    /// PPSSPP's own output is forwarded to our stderr, so it can be redirected separately from
    /// the protocol messages on stdout.
    #[arg(long, num_args = 1.., allow_hyphen_values = true)]
    launch: Vec<String>,

    /// Turn off the log and input broadcasts for this session. Both are pure noise for a script,
    /// and the log one is expensive - every line the emulator logs gets encoded as JSON and
    /// pushed down the socket, which throttles emulation badly on a heavy boot. Equivalent to
    /// sending broadcast.config.set yourself, minus the chance of also disabling 'stepping'.
    #[arg(long)]
    quiet: bool,
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
    "cpu.runUntilTime",
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

// Start PPSSPP and find out what debugger port it ended up on.
//
// The port is read from the child's own stdout as it appears, which is the whole point of doing
// this here rather than in a wrapper script: there's no log file to poll, no fixed sleep to guess
// at, and no window in which we could attach to a *previous* run that happens to still hold a
// port. PPSSPP logs the line at NOTICE precisely so it survives any --loglevel.
//
// Everything the child prints is forwarded to our stderr, so a caller can redirect the emulator's
// log without entangling it with the protocol messages we put on stdout.
fn launch_ppsspp(cmd: &[String]) -> Result<(std::process::Child, u16)> {
    use std::process::{Command, Stdio};

    let (exe, rest) = cmd.split_first().ok_or_else(|| anyhow!("--launch needs a command"))?;
    let mut args: Vec<String> = rest.to_vec();
    // Let the OS pick, unless the caller asked for a specific port themselves.
    if !args.iter().any(|a| a.starts_with("--debugger")) {
        args.push("--debugger=0".to_string());
    }

    let mut child = Command::new(exe)
        .args(&args)
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .spawn()
        .with_context(|| format!("Failed to start {exe}"))?;

    let stdout = child.stdout.take().expect("stdout was piped");
    let stderr = child.stderr.take().expect("stderr was piped");
    let (tx, rx) = mpsc::channel::<u16>();

    // Watch both streams for it. Which one it lands on depends on how that build routes its
    // logging, and guessing wrong just looks like "PPSSPP never reported a port" - so don't guess.
    fn forward(stream: impl io::Read + Send + 'static, tx: mpsc::Sender<u16>) {
        thread::spawn(move || {
            let mut sent = false;
            for line in io::BufReader::new(stream).lines().map_while(Result::ok) {
                if !sent {
                    if let Some(port) = parse_listening_port(&line) {
                        sent = tx.send(port).is_ok();
                    }
                }
                eprintln!("{line}");
            }
        });
    }
    forward(stdout, tx.clone());
    forward(stderr, tx);

    match rx.recv_timeout(Duration::from_secs(60)) {
        Ok(port) => Ok((child, port)),
        Err(_) => {
            child.kill().ok();
            child.wait().ok();
            Err(anyhow!(
                "PPSSPP never reported a debugger port. It logs that line at NOTICE, so it should \
                 survive any --loglevel - but a build predating that change logs it at INFO, where \
                 --loglevel=3 hides it. Check the forwarded output above for 'Listening on port'; \
                 if it isn't there, either raise --loglevel or pass an explicit --debugger=PORT."
            ))
        }
    }
}

// Matches either form Core/WebServer.cpp emits: the bare "Debugger listening on port N" written
// straight to stderr, and the "Entering web server loop. Listening on port N" log line (which an
// older build is the only source of). Both end in "listening on port N", so one match covers both.
fn parse_listening_port(line: &str) -> Option<u16> {
    let lower = line.to_ascii_lowercase();
    let idx = lower.find("listening on port ")? + "listening on port ".len();
    let digits: String = line[idx..].chars().take_while(|c| c.is_ascii_digit()).collect();
    digits.parse().ok()
}

// The socket usually isn't accepting the instant the port is printed, so retry rather than sleep
// for a guessed interval - too short is flaky, too long is dead time on every single run.
fn connect_with_retry(host: &str, port: u16, timeout: Duration) -> Result<WebSocket<TcpStream>> {
    let deadline = Instant::now() + timeout;
    loop {
        match connect(host, port) {
            Ok(socket) => return Ok(socket),
            Err(e) if Instant::now() >= deadline => return Err(e),
            Err(_) => thread::sleep(Duration::from_millis(50)),
        }
    }
}

// Silence the two broadcast categories a script never wants, without touching the ones it needs.
// Sent as a fixed message rather than left to the caller because hand-writing it is where the
// 'stepping' mistake gets made - see the warning in handle_repl_line.
fn quiet_broadcasts(socket: &mut WebSocket<TcpStream>) -> Result<()> {
    socket.send(Message::Text(
        serde_json::json!({
            "event": "broadcast.config.set",
            "ticket": next_ticket(),
            "disallowed": { "logger": true, "input": true },
        })
        .to_string()
        .into(),
    ))?;
    Ok(())
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

// Sent once per connection, before anything else, and its reply swallowed rather than printed -
// it's plumbing the user didn't ask for and shouldn't have to read past.
fn request_deferred_acks(socket: &mut WebSocket<TcpStream>) -> Result<()> {
    let ticket = next_ticket();
    socket.send(Message::Text(
        serde_json::json!({
            "event": "client.config.set",
            "acknowledgeDeferred": true,
            "ticket": ticket,
        })
        .to_string()
        .into(),
    ))?;

    socket.get_ref().set_read_timeout(Some(Duration::from_millis(50)))?;
    let deadline = Instant::now() + Duration::from_secs(2);
    while Instant::now() < deadline {
        match socket.read() {
            Ok(Message::Text(t)) => {
                let v = serde_json::from_str::<serde_json::Value>(&t).unwrap_or_default();
                if v.get("ticket").and_then(|t| t.as_u64()) == Some(ticket) {
                    return Ok(());
                }
                // Anything else this early is a broadcast we'd have printed anyway.
                print_incoming(&t);
            }
            Ok(_) => {}
            Err(e) if is_would_block(&e) => {}
            Err(e) => return Err(e.into()),
        }
    }
    Ok(())
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

// Returns false if the request went unanswered within the timeout, so one-shot mode can exit
// non-zero rather than looking successful after having printed nothing useful.
//
// With deferred acks turned on (see request_deferred_acks) every request is answered, including
// the ones whose result arrives later, so waiting for this request's ticket beats sleeping for a
// fixed --wait: a quick question returns immediately instead of padding every invocation, and a
// slow one isn't cut off early.
fn run_one_shot(
    mut socket: WebSocket<TcpStream>,
    json_text: String,
    ticket: Option<u64>,
    wait_secs: f64,
    wait_all: bool,
) -> Result<bool> {
    println!("-> {json_text}");
    socket.send(Message::Text(json_text.into()))?;

    socket.get_ref().set_read_timeout(Some(Duration::from_millis(50)))?;
    let deadline = Instant::now() + Duration::from_secs_f64(wait_secs.max(0.0));

    // Without a ticket (--raw with none supplied) there's nothing to match, so fall back to
    // draining until the deadline, same as --wait-all.
    let Some(ticket) = ticket.filter(|_| !wait_all) else {
        pump_until(&mut socket, deadline, |_| false);
        return Ok(true);
    };

    let answered = pump_until(&mut socket, deadline, |v| {
        v.get("ticket").and_then(|t| t.as_u64()) == Some(ticket)
    });
    if !answered {
        eprintln!("! timed out after {wait_secs}s waiting for a reply (ticket {ticket})");
    }
    Ok(answered)
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

// Turning off the 'stepping' broadcast breaks every "wait until the CPU stops again" mechanism we
// have - --sync's wait after a resume/step, and :wait cpu.stepping - because cpu.stepping is a
// broadcast with no ticket of its own and is the only signal that a run finished. The symptom is
// vicious: the emulator stops exactly when it should, then sits at 0% CPU while the script waits
// out its entire --sync-timeout, which reads as "the boot is slow" rather than "I muted the reply".
// Cheap to say so at the moment it's sent. Use --quiet for the safe version of this.
fn warn_if_stepping_muted(event: &str, obj: &serde_json::Map<String, serde_json::Value>) {
    if event != "broadcast.config.set" {
        return;
    }
    let muted = obj
        .get("disallowed")
        .and_then(|d| d.get("stepping"))
        .and_then(|s| s.as_bool())
        .unwrap_or(false);
    if muted {
        eprintln!(
            "! warning: disabling the 'stepping' broadcast means cpu.stepping never arrives, so \
             --sync and ':wait cpu.stepping' will block until they time out. Use --quiet instead."
        );
    }
}

// Returns the (ticket, event name) sent, when known - used by --sync to know what response to
// wait for. Both are recoverable for a plain "{...}" JSON paste too, as long as it includes
// "ticket"/"event" fields itself; if it doesn't (or ticket isn't a plain integer), --sync has
// nothing to wait on and just proceeds to the next line immediately, same as without --sync.
fn handle_repl_line(socket: &mut WebSocket<TcpStream>, line: &str) -> Result<Option<(u64, String)>> {
    if line.starts_with('{') {
        // A raw JSON line is the only way to send nested parameters, so it can't be a
        // second-class citizen. Reject it outright if it isn't valid JSON or has no event - but
        // send it exactly as written otherwise. Omitting the ticket is the documented way to say
        // "I'm not waiting for an answer", so quietly inserting one would send something the
        // author didn't write. --sync simply doesn't wait on such a line and moves on to the next
        // (it must not wait for "the next message that happens to arrive" and attribute that -
        // that's what desynchronised the rest of a script before tickets were matched properly).
        let parsed: serde_json::Value =
            serde_json::from_str(line).map_err(|e| anyhow!("Not valid JSON: {e}"))?;
        let obj = parsed
            .as_object()
            .ok_or_else(|| anyhow!("A raw message must be a JSON object"))?;
        let event = obj
            .get("event")
            .and_then(|e| e.as_str())
            .ok_or_else(|| anyhow!("A raw message needs a string 'event' field"))?
            .to_string();
        let ticket = match obj.get("ticket") {
            Some(t) => Some(
                t.as_u64()
                    .ok_or_else(|| anyhow!("'ticket' must be a non-negative integer to be matchable"))?,
            ),
            None => None,
        };
        warn_if_stepping_muted(&event, obj);
        match ticket {
            Some(t) => println!("-> (ticket {t}) {line}"),
            None => println!("-> (no ticket, not waiting for a reply) {line}"),
        }
        socket.send(Message::Text(line.to_string().into()))?;
        return Ok(ticket.map(|t| (t, event)));
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
// Every request is answered - either a response or an error, both carrying the request's ticket
// (PPSSPP acknowledges even the ones whose real result arrives later). So the ticket is always
// the thing to wait on, and matching on it is exact: no guessing from message order, and no list
// of events that don't answer.
//
// RESUME_FAMILY events additionally mean "let the CPU run", so after the acknowledgement we also
// wait for the following cpu.stepping broadcast - the actual "it stopped again" signal a script
// issuing one of these is really waiting for. That broadcast has no ticket of its own, so it's
// matched by event name.
//
// Returns false on timeout, which the caller turns into a non-zero exit.
fn wait_for_sync_response(
    socket: &mut WebSocket<TcpStream>,
    ticket: u64,
    event: &str,
    timeout_secs: f64,
) -> bool {
    let wants_stepping = RESUME_FAMILY.contains(&event);
    let mut got_ticket = false;
    let mut got_stepping = !wants_stepping;
    let deadline = Instant::now() + Duration::from_secs_f64(timeout_secs.max(0.0));

    let done = pump_until(socket, deadline, |v| {
        if v.get("ticket").and_then(|t| t.as_u64()) == Some(ticket) {
            got_ticket = true;
            // An error reply is a final answer - there'll be no cpu.stepping to follow.
            if v.get("event").and_then(|e| e.as_str()) == Some("error") {
                got_stepping = true;
            }
        }
        // Only counts once the request has been acknowledged, so a cpu.stepping still in flight
        // from something earlier can't be mistaken for this command's.
        if got_ticket && v.get("event").and_then(|e| e.as_str()) == Some("cpu.stepping") {
            got_stepping = true;
        }
        got_ticket && got_stepping
    });

    if !done {
        eprintln!(
            "! --sync: timed out after {timeout_secs}s waiting for '{event}' (ticket {ticket}){}",
            if got_ticket { ", acknowledged but never stopped" } else { "" }
        );
    }
    done
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
// Waits for replies to requests that were sent but never awaited, before the connection goes
// away. Without this, a script whose last line is a request followed by :quit exits before the
// answer arrives and looks exactly like the request silently doing nothing - which is a trap
// worth removing rather than documenting.
fn drain_pending(socket: &mut WebSocket<TcpStream>, pending: &mut HashSet<u64>, secs: f64) {
    if pending.is_empty() {
        return;
    }
    let deadline = Instant::now() + Duration::from_secs_f64(secs);
    while !pending.is_empty() && Instant::now() < deadline {
        match socket.read() {
            Ok(Message::Text(text)) => {
                print_incoming(&text);
                if let Ok(v) = serde_json::from_str::<serde_json::Value>(&text) {
                    if let Some(t) = v.get("ticket").and_then(|t| t.as_u64()) {
                        pending.remove(&t);
                    }
                }
            }
            Ok(_) => {}
            Err(ref e) if is_would_block(e) => {}
            Err(_) => return,
        }
    }
    if !pending.is_empty() {
        eprintln!(
            "! exiting with {} request(s) still unanswered after {}s - their replies were lost",
            pending.len(),
            secs
        );
    }
}

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
    // Requests sent whose reply hasn't been seen yet, so :quit can wait for them.
    let mut pending: HashSet<u64> = HashSet::new();

    loop {
        match rx.try_recv() {
            Ok(line) => {
                let line = line.trim();
                let mut words = line.split_whitespace();
                match words.next() {
                    Some(":quit") | Some(":q") | Some(":exit") => {
                        drain_pending(&mut socket, &mut pending, 10.0);
                        return Ok(!failed);
                    }
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
                        Err(e) => {
                            // A line that couldn't even be sent means the script didn't do what it
                            // says it does - don't let that pass as success.
                            eprintln!("! {e}");
                            failed = true;
                        }
                        Ok(Some((ticket, event))) if sync => {
                            if !wait_for_sync_response(&mut socket, ticket, &event, sync_timeout) {
                                failed = true;
                            }
                        }
                        Ok(Some((ticket, _))) => {
                            pending.insert(ticket);
                        }
                        Ok(_) => {}
                    },
                }
                print_prompt();
            }
            Err(mpsc::TryRecvError::Disconnected) => {
                // Piped script hit EOF - same race as :quit.
                drain_pending(&mut socket, &mut pending, 10.0);
                return Ok(!failed);
            }
            Err(mpsc::TryRecvError::Empty) => {}
        }

        match socket.read() {
            Ok(Message::Text(text)) => {
                if let Ok(v) = serde_json::from_str::<serde_json::Value>(&text) {
                    if let Some(t) = v.get("ticket").and_then(|t| t.as_u64()) {
                        pending.remove(&t);
                    }
                }
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

    // With --launch we own the emulator and have to take it down again on the way out - including
    // on the std::process::exit paths below, which skip destructors, so this can't be a Drop guard.
    let (mut child, port) = if !args.launch.is_empty() {
        if args.port.is_some() {
            return Err(anyhow!("Pass either a port or --launch, not both"));
        }
        let (child, port) = launch_ppsspp(&args.launch)?;
        if !compact() {
            println!("Launched PPSSPP (pid {}), debugger on port {port}", child.id());
        }
        (Some(child), port)
    } else {
        let port = args
            .port
            .ok_or_else(|| anyhow!("Need a port to connect to, or --launch to start PPSSPP"))?;
        (None, port)
    };

    // Failing to reach a process we just started is a different problem from failing to reach one
    // that was supposed to be there already, so only retry in the case where we know it's coming up.
    let connect_result = if child.is_some() {
        connect_with_retry(&args.host, port, Duration::from_secs(30))
    } else {
        connect(&args.host, port)
    };
    let mut socket = match connect_result {
        Ok(socket) => socket,
        Err(e) => {
            if let Some(c) = child.as_mut() {
                c.kill().ok();
                c.wait().ok();
            }
            return Err(e).context(
                "Could not connect. Is PPSSPP running with the WebSocket debugger enabled? \
                 (Settings > Tools > Developer Tools > Allow remote debugger, or launch with --debugger)",
            );
        }
    };

    let finish = |child: Option<std::process::Child>, ok: bool| -> ! {
        if let Some(mut c) = child {
            c.kill().ok();
            c.wait().ok();
        }
        std::process::exit(if ok { 0 } else { 1 })
    };

    // Ask to be told when a request was accepted but finishes later (cpu.resume and friends).
    // Off by default server-side, since the extra message would confuse a client that correlates
    // purely by ticket - but it's exactly what lets --sync match every request to a reply without
    // knowing which events answer immediately. Ignore the error from an older PPSSPP that doesn't
    // know the event; --sync just falls back to timing out on those, as it did before.
    request_deferred_acks(&mut socket).ok();

    if args.quiet {
        quiet_broadcasts(&mut socket).ok();
    }

    if let Some(raw) = &args.raw {
        // Recover the ticket if the caller supplied one; there's nothing to wait on otherwise.
        let ticket = serde_json::from_str::<serde_json::Value>(raw)
            .ok()
            .and_then(|v| v.get("ticket").and_then(|t| t.as_u64()));
        let ok = run_one_shot(socket, raw.clone(), ticket, args.wait, args.wait_all)?;
        finish(child, ok);
    }

    if let Some(event) = &args.event {
        let ticket = next_ticket();
        let json_text = build_event_json(event, &args.params, Some(ticket))?;
        let ok = run_one_shot(socket, json_text, Some(ticket), args.wait, args.wait_all)?;
        finish(child, ok);
    }

    // Non-zero exit when something in the script failed, so a caller doesn't have to grep output
    // to find out whether its :wait ever fired.
    let ok = run_repl(socket, args.sync, args.sync_timeout)?;
    finish(child, ok);
}
