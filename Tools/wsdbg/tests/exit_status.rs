// Copyright (c) 2012- PPSSPP Project.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License 2.0 for more details.

use std::io::Write;
use std::net::{TcpListener, TcpStream};
use std::process::{Command, Output, Stdio};
use std::thread;
use std::time::{Duration, Instant};

use serde_json::{Value, json};
use tungstenite::handshake::server::{Request, Response};
use tungstenite::{Message, WebSocket};

// Exercise the real command-line client against a debugger peer, without booting a game.
fn run_client(
    args: &[&str],
    script: Option<&str>,
    replies: impl FnOnce(&Value) -> Vec<Value>,
) -> (Output, Duration) {
    let listener = TcpListener::bind(("127.0.0.1", 0)).unwrap();
    listener.set_nonblocking(true).unwrap();
    let mut child = Command::new(env!("CARGO_BIN_EXE_wsdbg"))
        .arg(listener.local_addr().unwrap().port().to_string())
        .arg("--compact")
        .args(args)
        .stdin(Stdio::piped())
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .spawn()
        .unwrap();
    if let Some(script) = script {
        child
            .stdin
            .take()
            .unwrap()
            .write_all(script.as_bytes())
            .unwrap();
    } else {
        drop(child.stdin.take());
    }

    let deadline = Instant::now() + Duration::from_secs(5);
    let stream = loop {
        match listener.accept() {
            Ok((stream, _)) => break stream,
            Err(e) if e.kind() == std::io::ErrorKind::WouldBlock && Instant::now() < deadline => {
                thread::sleep(Duration::from_millis(5));
            }
            Err(e) => {
                child.kill().ok();
                let output = child.wait_with_output().unwrap();
                panic!("client did not connect: {e}; {output:?}");
            }
        }
    };
    stream
        .set_read_timeout(Some(Duration::from_secs(2)))
        .unwrap();
    stream
        .set_write_timeout(Some(Duration::from_secs(2)))
        .unwrap();
    // The callback's error type is fixed by tungstenite.
    #[allow(clippy::result_large_err)]
    let mut socket = tungstenite::accept_hdr(stream, |_: &Request, mut response: Response| {
        response.headers_mut().insert(
            "Sec-WebSocket-Protocol",
            "debugger.ppsspp.org".parse().unwrap(),
        );
        Ok(response)
    })
    .unwrap();

    let request = loop {
        let value = read_json(&mut socket);
        match value["event"].as_str().unwrap() {
            "client.config.set" | "version" => {
                socket.send(Message::Text(value.to_string())).unwrap();
            }
            _ => break value,
        }
    };
    let response_time = Instant::now();
    for reply in replies(&request) {
        socket.send(Message::Text(reply.to_string())).unwrap();
    }

    // Keep the peer connected: an error must finish the request itself, without relying on EOF.
    let deadline = Instant::now() + Duration::from_secs(5);
    while child.try_wait().unwrap().is_none() {
        if Instant::now() >= deadline {
            child.kill().ok();
            let output = child.wait_with_output().unwrap();
            panic!("client did not exit: {output:?}");
        }
        thread::sleep(Duration::from_millis(5));
    }
    let elapsed = response_time.elapsed();
    (child.wait_with_output().unwrap(), elapsed)
}

fn read_json(socket: &mut WebSocket<TcpStream>) -> Value {
    loop {
        if let Message::Text(text) = socket.read().unwrap() {
            return serde_json::from_str(&text).unwrap();
        }
    }
}

fn error_reply(request: &Value) -> Value {
    json!({"event": "error", "ticket": request["ticket"], "message": "CPU not started"})
}

fn assert_success(output: &Output, expected: bool) {
    assert_eq!(
        output.status.success(),
        expected,
        "stdout: {}\nstderr: {}",
        String::from_utf8_lossy(&output.stdout),
        String::from_utf8_lossy(&output.stderr),
    );
}

#[test]
fn one_shot_reports_server_errors() {
    let (output, _) = run_client(&["cpu.status"], None, |request| vec![error_reply(request)]);
    assert_success(&output, false);
}

#[test]
fn one_shot_ignores_unrelated_errors() {
    let (output, _) = run_client(&["cpu.status"], None, |request| {
        vec![
            json!({"event": "error", "ticket": 9999, "message": "another request failed"}),
            json!({"event": "cpu.status", "ticket": request["ticket"], "stepping": true}),
        ]
    });
    assert_success(&output, true);
}

#[test]
fn wait_all_reports_server_errors() {
    let (output, _) = run_client(
        &["--wait-all", "--wait", "0.2", "cpu.status"],
        None,
        |request| vec![error_reply(request)],
    );
    assert_success(&output, false);
}

#[test]
fn wait_all_ignores_unrelated_errors() {
    let (output, _) = run_client(
        &["--wait-all", "--wait", "0.2", "cpu.status"],
        None,
        |request| {
            vec![
                json!({"event": "error", "ticket": 9999, "message": "another request failed"}),
                json!({"event": "cpu.status", "ticket": request["ticket"], "stepping": true}),
            ]
        },
    );
    assert_success(&output, true);
}

#[test]
fn wait_all_keeps_failure_after_later_reply() {
    let (output, _) = run_client(
        &["--wait-all", "--wait", "0.2", "cpu.status"],
        None,
        |request| {
            vec![
                error_reply(request),
                json!({"event": "cpu.status", "ticket": request["ticket"], "stepping": true}),
            ]
        },
    );
    assert_success(&output, false);
}

#[test]
fn raw_without_ticket_remains_observational() {
    let (output, _) = run_client(
        &["--wait", "0.2", "--raw", r#"{"event":"cpu.status"}"#],
        None,
        |request| {
            assert!(request.get("ticket").is_none());
            vec![json!({"event": "error", "message": "CPU not started"})]
        },
    );
    assert_success(&output, true);
}

#[test]
fn sync_reports_server_errors() {
    let (output, _) = run_client(&["--sync"], Some("cpu.status\n:quit\n"), |request| {
        vec![error_reply(request)]
    });
    assert_success(&output, false);
}

#[test]
fn sync_resume_error_does_not_wait_for_stepping() {
    let (output, elapsed) = run_client(
        &["--sync", "--sync-timeout", "3"],
        Some("cpu.resume\n:quit\n"),
        |request| vec![error_reply(request)],
    );
    assert_success(&output, false);
    assert!(elapsed < Duration::from_secs(2), "error took {elapsed:?}");
    assert!(!String::from_utf8_lossy(&output.stderr).contains("timed out"));
}

#[test]
fn sync_ignores_unrelated_errors() {
    let (output, _) = run_client(&["--sync"], Some("cpu.status\n:quit\n"), |request| {
        vec![
            json!({"event": "error", "ticket": 9999, "message": "another request failed"}),
            json!({"event": "cpu.status", "ticket": request["ticket"], "stepping": true}),
        ]
    });
    assert_success(&output, true);
}

#[test]
fn sync_resume_accepts_deferred_ack_and_stepping() {
    let (output, _) = run_client(&["--sync"], Some("cpu.resume\n:quit\n"), |request| {
        vec![
            json!({"event": "deferred", "ticket": request["ticket"], "for": "cpu.resume"}),
            json!({"event": "cpu.stepping", "pc": 0x08800000}),
        ]
    });
    assert_success(&output, true);
}
