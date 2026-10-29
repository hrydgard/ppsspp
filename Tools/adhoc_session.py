#!/usr/bin/env python3
# Runs several PPSSPP headless instances of a game against each other over ad hoc, and drives them over
# the WebSocket debugger: press buttons, take screenshots, read frame rates. For testing a game's
# multiplayer, and for measuring what network delay it tolerates. See docs/adhoc-multiplayer-testing.md.
#
#   python3 Tools/adhoc_session.py start ~/PSP/GAME.iso -n 2 [--delay 50] [--state FILE]
#   python3 Tools/adhoc_session.py press cross -i 2
#   python3 Tools/adhoc_session.py wait 3
#   python3 Tools/adhoc_session.py shot lobby
#   python3 Tools/adhoc_session.py stats
#   python3 Tools/adhoc_session.py run script.txt     (lines like the commands above, without the prefix)
#   python3 Tools/adhoc_session.py stop
#
# The instances run freely at PSP speed (--realtime, and --lag-sync unless --no-lag-sync), and each one
# gets its own memory stick with a copy of the real one's savedata, so they can't overwrite each other's
# (or your) saves. The firmware comes from the real memory stick's NAND.

import argparse
import base64
import glob
import json
import os
import re
import shutil
import socket
import struct
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_DIR = os.path.join(os.environ.get("TMPDIR", "/tmp"), "ppsspp-adhoc-session")
DEFAULT_MEMSTICK = os.path.expanduser("~/.config/ppsspp")


class Debugger:
	"""A minimal WebSocket client for PPSSPP's debugger, standard library only."""

	def __init__(self, port):
		self.sock = socket.create_connection(("127.0.0.1", port), timeout=30)
		key = base64.b64encode(os.urandom(16)).decode()
		self.sock.sendall(("GET /debugger HTTP/1.1\r\nHost: 127.0.0.1:%d\r\nUpgrade: websocket\r\n"
			"Connection: Upgrade\r\nSec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\n"
			"Sec-WebSocket-Protocol: debugger.ppsspp.org\r\n\r\n" % (port, key)).encode())
		response = b""
		while b"\r\n\r\n" not in response:
			chunk = self.sock.recv(4096)
			if not chunk:
				raise ConnectionError("debugger closed the connection during the handshake")
			response += chunk
		if b" 101 " not in response.split(b"\r\n")[0]:
			raise ConnectionError("debugger refused the WebSocket upgrade: %r" % response[:80])
		self.buffer = response.split(b"\r\n\r\n", 1)[1]
		self.ticket = 0
		self.send("broadcast.config.set", disallowed={"logger": True, "input": True})

	def close(self):
		try:
			self.sock.close()
		except OSError:
			pass

	def _read(self, n):
		while len(self.buffer) < n:
			chunk = self.sock.recv(65536)
			if not chunk:
				raise ConnectionError("debugger closed the connection")
			self.buffer += chunk
		data, self.buffer = self.buffer[:n], self.buffer[n:]
		return data

	def _frame(self):
		b0, b1 = self._read(2)
		length = b1 & 0x7F
		if length == 126:
			length = struct.unpack(">H", self._read(2))[0]
		elif length == 127:
			length = struct.unpack(">Q", self._read(8))[0]
		mask = self._read(4) if b1 & 0x80 else None
		payload = self._read(length)
		if mask:
			payload = bytes(c ^ mask[i % 4] for i, c in enumerate(payload))
		return b0 & 0x80, b0 & 0x0F, payload

	def receive(self):
		message = b""
		while True:
			fin, opcode, payload = self._frame()
			if opcode == 0x8:
				raise ConnectionError("debugger closed the connection")
			if opcode == 0x9:
				self._send_frame(0xA, payload)
				continue
			if opcode == 0xA:
				continue
			message += payload
			if fin:
				return json.loads(message.decode("utf-8", "replace"))

	def _send_frame(self, opcode, payload):
		mask = os.urandom(4)
		header = bytes([0x80 | opcode])
		if len(payload) < 126:
			header += bytes([0x80 | len(payload)])
		elif len(payload) < 65536:
			header += bytes([0x80 | 126]) + struct.pack(">H", len(payload))
		else:
			header += bytes([0x80 | 127]) + struct.pack(">Q", len(payload))
		self.sock.sendall(header + mask + bytes(c ^ mask[i % 4] for i, c in enumerate(payload)))

	def send(self, event, **params):
		self.ticket += 1
		self._send_frame(0x1, json.dumps(dict(params, event=event, ticket=self.ticket)).encode())
		return self.ticket

	def wait_for(self, predicate, timeout=30):
		self.sock.settimeout(timeout)
		end = time.time() + timeout
		while time.time() < end:
			message = self.receive()
			if message.get("event") == "error" and message.get("ticket") is not None:
				raise RuntimeError(message.get("message"))
			if predicate(message):
				return message
		raise TimeoutError("no answer from the debugger")

	def request(self, event, timeout=30, **params):
		ticket = self.send(event, **params)
		return self.wait_for(lambda m: m.get("ticket") == ticket, timeout)

	def stopped(self):
		return self.request("cpu.status").get("stepping", False)

	def stop_cpu(self):
		if not self.stopped():
			self.send("cpu.stepping")
			self.wait_for(lambda m: m.get("event") == "cpu.stepping")

	def resume_cpu(self):
		if self.stopped():
			self.send("cpu.resume")
			self.wait_for(lambda m: m.get("event") == "cpu.resume")


def session_file(args):
	return os.path.join(args.dir, "session.json")


def load_session(args):
	try:
		with open(session_file(args)) as f:
			return json.load(f)
	except FileNotFoundError:
		sys.exit("No session in %s; start one first" % args.dir)


def selected(session, which):
	if not which:
		return session["instances"]
	wanted = [int(x) for x in which.split(",")]
	return [inst for inst in session["instances"] if inst["n"] in wanted]


def find_headless():
	candidates = glob.glob(os.path.join(ROOT, "build*", "PPSSPPHeadless")) + glob.glob(os.path.join(ROOT, "build*", "PPSSPPHeadless.exe"))
	if not candidates:
		sys.exit("No PPSSPPHeadless found in build*/; pass --headless")
	return max(candidates, key=os.path.getmtime)


def loopback_aliases_missing(n):
	try:
		out = subprocess.run(["ifconfig", "lo0"], capture_output=True, text=True).stdout
	except FileNotFoundError:
		return []  # Linux routes all of 127.0.0.0/8 to lo already.
	return [i for i in range(2, n + 1) if "inet 127.0.0.%d " % i not in out]


def cmd_start(args):
	if os.path.exists(session_file(args)):
		cmd_stop(args)
	if args.p2p:
		missing = loopback_aliases_missing(args.n)
		if missing:
			sys.exit("P2P mode needs a loopback address per instance. Add them with:\n" +
				"\n".join("  sudo ifconfig lo0 alias 127.0.0.%d up" % i for i in missing))
	headless = args.headless or find_headless()
	os.makedirs(args.dir, exist_ok=True)
	ini = os.path.join(args.dir, "network.ini")
	with open(ini, "w") as f:
		f.write("[Network]\nproAdhocServer = 127.0.0.1\nAdhocServerRelayMode = %d\n" % (0 if args.p2p else 1))
	instances = []
	for n in range(1, args.n + 1):
		memstick = os.path.join(args.dir, "memstick%d" % n)
		shutil.rmtree(memstick, ignore_errors=True)
		shutil.copytree(os.path.join(args.memstick, "PSP", "SAVEDATA"), os.path.join(memstick, "PSP", "SAVEDATA"))
		log = os.path.join(args.dir, "instance%d.log" % n)
		cmd = [headless, "--graphics=" + args.graphics, "--memstick=" + memstick,
			"--nand=" + os.path.join(args.memstick, "PSP", "NAND"), "--realtime", "--instance=%d" % n,
			"--appendconfig=" + ini, "--debugger-run=0", "--timeout-wall=%d" % args.timeout]
		if not args.no_lag_sync:
			cmd.append("--lag-sync")
		if n == 1:
			cmd.append("--adhoc-server")
		if args.delay:
			cmd.append("--adhoc-delay=%d" % args.delay)
		if args.jitter:
			cmd.append("--adhoc-jitter=%d" % args.jitter)
		if args.loss:
			cmd.append("--adhoc-loss=%d" % args.loss)
		if args.log:
			cmd.append("--log")
		if args.state:
			cmd.append("--state=" + args.state)
		cmd += args.extra or []
		cmd.append(args.game)
		with open(log, "w") as out:
			proc = subprocess.Popen(cmd, stdout=out, stderr=subprocess.STDOUT, start_new_session=True)
		instances.append({"n": n, "pid": proc.pid, "log": log, "port": None})
		if n == 1:
			# Let the built-in server come up before the others log in.
			time.sleep(1.0)
	for inst in instances:
		for _ in range(100):
			with open(inst["log"], errors="replace") as f:
				text = f.read()
			match = re.search(r"[Ll]istening on port (\d+)", text)
			if match:
				inst["port"] = int(match.group(1))
				break
			time.sleep(0.1)
		if inst["port"] is None:
			sys.exit("Instance %d didn't open a debugger port; see %s" % (inst["n"], inst["log"]))
	session = {"game": args.game, "instances": instances, "delay": args.delay, "jitter": args.jitter,
		"loss": args.loss, "lagSync": not args.no_lag_sync, "p2p": args.p2p}
	with open(session_file(args), "w") as f:
		json.dump(session, f, indent=1)
	print("Started %d instances (%s%s), session in %s" % (args.n, "P2P" if args.p2p else "relay",
		", delay %d ms" % args.delay if args.delay else "", args.dir))


def cmd_stop(args):
	try:
		with open(session_file(args)) as f:
			session = json.load(f)
	except FileNotFoundError:
		return
	for inst in session["instances"]:
		try:
			os.kill(inst["pid"], 15)
		except ProcessLookupError:
			pass
	os.remove(session_file(args))
	print("Stopped")


def cmd_press(args):
	session = load_session(args)
	for inst in selected(session, args.i):
		dbg = Debugger(inst["port"])
		# The answer only comes once the press is over, so don't wait for it.
		dbg.send("input.buttons.press", button=args.button, duration=args.frames)
		dbg.close()


def cmd_wait(args):
	time.sleep(args.seconds)


def cmd_shot(args):
	session = load_session(args)
	files = []
	for inst in selected(session, args.i):
		dbg = Debugger(inst["port"])
		dbg.stop_cpu()
		answer = dbg.request("gpu.buffer.screenshot", type="uri")
		dbg.resume_cpu()
		dbg.close()
		path = os.path.join(args.dir, "%s_%d.png" % (args.name, inst["n"]))
		with open(path, "wb") as f:
			f.write(base64.b64decode(answer["uri"].split(",", 1)[1]))
		files.append(path)
	try:
		from PIL import Image
		images = [Image.open(p) for p in files]
		combined = Image.new("RGB", (sum(i.width for i in images) + 8 * (len(images) - 1), max(i.height for i in images)), "white")
		x = 0
		for image in images:
			combined.paste(image, (x, 0))
			x += image.width + 8
		path = os.path.join(args.dir, args.name + ".png")
		combined.save(path)
		print(path)
	except ImportError:
		print("\n".join(files))


def cmd_stats(args):
	session = load_session(args)
	for inst in selected(session, args.i):
		dbg = Debugger(inst["port"])
		status = dbg.request("cpu.status")
		stats = dbg.request("gpu.stats.get", timeout=5)
		dbg.close()
		print("instance %d: %.1f s emulated, %.1f fps, %.1f vblanks/s" % (inst["n"], status["us"] / 1e6,
			stats["fps"]["actual"], stats["vblanksPerSecond"]["actual"]))


COMMANDS = {}


def cmd_run(args):
	with open(args.script) as f:
		for line in f:
			line = line.split("#", 1)[0].strip()
			if not line:
				continue
			print("> " + line)
			sub = build_parser().parse_args(["--dir", args.dir] + line.split())
			COMMANDS[sub.command](sub)


def build_parser():
	parser = argparse.ArgumentParser(description="Run and drive several PPSSPP instances over ad hoc")
	parser.add_argument("--dir", default=DEFAULT_DIR, help="Where the session keeps its memory sticks, logs and screenshots")
	sub = parser.add_subparsers(dest="command", required=True)

	p = sub.add_parser("start", help="Start N instances of a game")
	p.add_argument("game")
	p.add_argument("-n", type=int, default=2, help="Number of instances (default 2)")
	p.add_argument("--p2p", action="store_true", help="Talk directly instead of through the relay (needs a loopback address per instance)")
	p.add_argument("--delay", type=int, default=0, help="Simulated delay in ms on received relay datagrams (each way)")
	p.add_argument("--jitter", type=int, default=0, help="Up to this many ms more, at random")
	p.add_argument("--loss", type=int, default=0, help="Percentage of received relay datagrams dropped")
	p.add_argument("--no-lag-sync", action="store_true", help="Pace once per frame, like the app's default")
	p.add_argument("--state", help="Savestate to load at boot in every instance")
	p.add_argument("--memstick", default=DEFAULT_MEMSTICK, help="Memory stick to take the firmware and savedata from")
	p.add_argument("--graphics", default="vulkan")
	p.add_argument("--headless", help="PPSSPPHeadless to run (default: the newest build*/ one)")
	p.add_argument("--timeout", type=int, default=3600, help="Wall-clock seconds before the instances quit")
	p.add_argument("--log", action="store_true", help="Pass --log (large logs)")
	p.add_argument("--extra", nargs=argparse.REMAINDER, help="More arguments for PPSSPPHeadless")

	p = sub.add_parser("press", help="Press a button")
	p.add_argument("button", help="cross, circle, square, triangle, up, down, left, right, start, select, ltrigger, rtrigger")
	p.add_argument("-i", help="Instances, like 1,2 (default all)")
	p.add_argument("--frames", type=int, default=6, help="How many vblanks to hold it")

	p = sub.add_parser("wait", help="Wait (wall clock)")
	p.add_argument("seconds", type=float)

	p = sub.add_parser("shot", help="Screenshot each instance, and all side by side as NAME.png")
	p.add_argument("name")
	p.add_argument("-i")

	p = sub.add_parser("stats", help="Emulated time and frame rate of each instance")
	p.add_argument("-i")

	p = sub.add_parser("run", help="Run a script of these commands, one per line")
	p.add_argument("script")

	sub.add_parser("stop", help="Stop the instances")
	return parser


COMMANDS.update(start=cmd_start, stop=cmd_stop, press=cmd_press, wait=cmd_wait, shot=cmd_shot, stats=cmd_stats, run=cmd_run)


def main():
	args = build_parser().parse_args()
	COMMANDS[args.command](args)


if __name__ == "__main__":
	main()
