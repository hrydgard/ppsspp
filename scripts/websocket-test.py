# Initially by Nemoumbra, extended to support more parameters and receive responses by ChatGPT.
# Example usage from the root:
# > python scripts\websocket-test.py 56244 gpu.stats.get
# NOTE: For some reason fails to connect from WSL, this should be investigated.

import sys
import time
from websocket import WebSocket
from json import dumps


def main():
    if len(sys.argv) not in (3, 4):
        print(f"Usage: {sys.argv[0]} <port> <cmd> [wait_secs]")
        print("Example commands: gpu.stats.get game.reset game.status (there are more)")
        print("Default wait time: 2 seconds")
        sys.exit(1)

    # Validate port
    try:
        port = int(sys.argv[1])
        if not (1 <= port <= 65535):
            raise ValueError("Port must be between 1 and 65535")
    except ValueError as e:
        print(f"Invalid port: {e}")
        sys.exit(1)

    cmd = sys.argv[2]

    # Parse wait time (default = 2)
    try:
        wait_secs = int(sys.argv[3]) if len(sys.argv) == 4 else 2
        if wait_secs < 0:
            raise ValueError("Wait time must be non-negative")
    except ValueError as e:
        print(f"Invalid wait_secs: {e}")
        sys.exit(1)

    host = "127.0.0.1"
    uri = f"ws://{host}:{port}/debugger"

    ws = WebSocket()
    try:
        ws.connect(uri)
        request = {"event": cmd}
        ws.send(dumps(request))
        print(f"Sent {cmd} event to {uri}, listening for {wait_secs} second(s)...")

        ws.settimeout(wait_secs)
        start = time.time()
        while True:
            try:
                response = ws.recv()
                print("Received response:", response)
            except Exception:
                # Stop when timeout occurs or no more messages
                break
            if time.time() - start > wait_secs:
                break

    except Exception as e:
        print(f"Connection failed: {e}")
    finally:
        ws.close()


if __name__ == "__main__":
    main()