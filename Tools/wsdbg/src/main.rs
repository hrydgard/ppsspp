// wsdbg - a tiny interactive client for the PPSSPP WebSocket debugger interface.
//
// See docs/WebSocketDebugger.md in the root of the ppsspp repo for the protocol reference.
//
// Examples:
//   wsdbg 12345                        # interactive REPL
//   wsdbg 12345 game.status            # one-shot: send an event, print responses, exit
//   wsdbg 12345 cpu.setReg thread=0 name=0 value=42

use std::io::{self, BufRead, Write};
use std::net::TcpStream;
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::mpsc;
use std::thread;
use std::time::{Duration, Instant};

use anyhow::{Context, Result, anyhow};
use clap::Parser;
use tungstenite::client::IntoClientRequest;
use tungstenite::http::HeaderValue;
use tungstenite::{Message, WebSocket};

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
}

static TICKET: AtomicU64 = AtomicU64::new(1);

fn next_ticket() -> u64 {
    TICKET.fetch_add(1, Ordering::Relaxed)
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
    match serde_json::from_str::<serde_json::Value>(text) {
        Ok(v) => println!("\n<- {}", serde_json::to_string_pretty(&v).unwrap_or_else(|_| text.to_string())),
        Err(_) => println!("\n<- {text}"),
    }
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
    println!(":help            show this message");
    println!(":quit / :q       disconnect and exit");
    println!("See docs/WebSocketDebugger.md in the ppsspp repo for the full event catalog.");
}

fn handle_repl_line(socket: &mut WebSocket<TcpStream>, line: &str) -> Result<()> {
    let json_text = if line.starts_with('{') {
        line.to_string()
    } else {
        let mut parts = line.split_whitespace();
        let event = parts.next().ok_or_else(|| anyhow!("Empty command"))?;
        let rest: Vec<String> = parts.map(|s| s.to_string()).collect();
        let ticket = next_ticket();
        let json_text = build_event_json(event, &rest, Some(ticket))?;
        println!("-> (ticket {ticket}) {json_text}");
        socket.send(Message::Text(json_text.into()))?;
        return Ok(());
    };

    println!("-> {json_text}");
    socket.send(Message::Text(json_text.into()))?;
    Ok(())
}

fn run_repl(mut socket: WebSocket<TcpStream>) -> Result<()> {
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

    print_help();

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

    print!("> ");
    io::stdout().flush().ok();

    loop {
        match rx.try_recv() {
            Ok(line) => {
                let line = line.trim();
                match line {
                    ":quit" | ":q" | ":exit" => return Ok(()),
                    ":help" | ":h" => print_help(),
                    "" => {}
                    _ => {
                        if let Err(e) = handle_repl_line(&mut socket, line) {
                            eprintln!("! {e}");
                        }
                    }
                }
                print!("> ");
                io::stdout().flush().ok();
            }
            Err(mpsc::TryRecvError::Disconnected) => return Ok(()),
            Err(mpsc::TryRecvError::Empty) => {}
        }

        match socket.read() {
            Ok(Message::Text(text)) => {
                print_incoming(&text);
                print!("> ");
                io::stdout().flush().ok();
            }
            Ok(Message::Close(frame)) => {
                println!("\n[connection closed by PPSSPP: {frame:?}]");
                return Ok(());
            }
            Ok(_) => {}
            Err(ref e) if is_would_block(e) => {}
            Err(tungstenite::Error::ConnectionClosed | tungstenite::Error::AlreadyClosed) => {
                println!("\n[connection closed]");
                return Ok(());
            }
            Err(e) => {
                eprintln!("\n[connection error: {e}]");
                return Ok(());
            }
        }
    }
}

fn main() -> Result<()> {
    let args = Args::parse();

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

    run_repl(socket)
}
