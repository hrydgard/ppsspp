#!/usr/bin/env python3
"""Serves game files over HTTP with range requests, which PPSSPP streams games from (http://host:port/name).

For an Android device over adb, without copying the game to it:

    python Tools/serve_games.py --adb SERIAL "//server/psp iso" ~/psp
    adb -s SERIAL shell "cd /data/local/tmp/x && ./ppsspp_headless 'http://127.0.0.1:8123/Some%20Game.iso' ..."

--adb runs `adb reverse`, so the device reaches this machine's server at 127.0.0.1. Without it, use this
machine's address. A name is looked up in each directory in turn (not in subdirectories), and / lists them all.
"""

import argparse
import http.server
import os
import subprocess
import sys
import urllib.parse


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    dirs = []
    quiet = False

    def find(self):
        name = urllib.parse.unquote(urllib.parse.urlsplit(self.path).path).lstrip("/")
        if not name or "/" in name or "\\" in name or name in (".", ".."):
            return None
        for d in self.dirs:
            path = os.path.join(d, name)
            if os.path.isfile(path):
                return path
        return None

    def send_listing(self, head):
        names = sorted({n for d in self.dirs for n in os.listdir(d) if os.path.isfile(os.path.join(d, n))})
        body = "".join(urllib.parse.quote(n) + "\n" for n in names).encode()
        self.send_response(200)
        self.send_header("Content-Type", "text/plain; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        if not head:
            self.wfile.write(body)

    def serve(self, head):
        if urllib.parse.urlsplit(self.path).path == "/":
            return self.send_listing(head)
        path = self.find()
        if not path:
            self.send_error(404)
            return
        size = os.path.getsize(path)
        start, end = 0, size - 1
        partial = False
        rng = self.headers.get("Range")
        if rng and rng.startswith("bytes="):
            first, _, last = rng[6:].split(",")[0].partition("-")
            try:
                if first:
                    start = int(first)
                    end = min(int(last), size - 1) if last else size - 1
                else:
                    start = max(0, size - int(last))
            except ValueError:
                start = size
            if start >= size or end < start:
                self.send_response(416)
                self.send_header("Content-Range", "bytes */%d" % size)
                self.send_header("Content-Length", "0")
                self.end_headers()
                return
            partial = True
        self.send_response(206 if partial else 200)
        self.send_header("Content-Type", "application/octet-stream")
        self.send_header("Accept-Ranges", "bytes")
        self.send_header("Content-Length", str(end - start + 1))
        if partial:
            self.send_header("Content-Range", "bytes %d-%d/%d" % (start, end, size))
        self.end_headers()
        if head:
            return
        with open(path, "rb") as f:
            f.seek(start)
            left = end - start + 1
            while left > 0:
                chunk = f.read(min(left, 1 << 20))
                if not chunk:
                    break
                self.wfile.write(chunk)
                left -= len(chunk)

    def do_GET(self):
        self.serve(False)

    def do_HEAD(self):
        self.serve(True)

    def log_message(self, fmt, *args):
        if not self.quiet:
            super().log_message(fmt, *args)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("dirs", nargs="+", help="directories holding the games")
    parser.add_argument("--port", type=int, default=8123)
    parser.add_argument("--adb", metavar="SERIAL", action="append", default=[],
                        help="forward the port from this device with adb reverse (repeatable)")
    parser.add_argument("--quiet", action="store_true", help="don't log requests")
    args = parser.parse_args()

    Handler.dirs = [os.path.expanduser(d) for d in args.dirs]
    Handler.quiet = args.quiet
    for d in Handler.dirs:
        if not os.path.isdir(d):
            sys.exit("Not a directory: " + d)
    for serial in args.adb:
        subprocess.run(["adb", "-s", serial, "reverse", "tcp:%d" % args.port, "tcp:%d" % args.port], check=True)
    # Only local connections with --adb, which is all adb reverse makes.
    server = http.server.ThreadingHTTPServer(("127.0.0.1" if args.adb else "", args.port), Handler)
    print("Serving on port %d" % args.port, flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
