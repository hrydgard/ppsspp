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
import threading
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_DIR = os.path.join(os.environ.get("TMPDIR", "/tmp"), "ppsspp-adhoc-session")
DEFAULT_MEMSTICK = os.path.expanduser("~/.config/ppsspp")


class Debugger:
	"""A minimal WebSocket client for PPSSPP's debugger, standard library only."""

	def __init__(self, port, logs=False):
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
		self.send("broadcast.config.set", disallowed={"logger": not logs, "input": True})

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
	# The last session's screenshots would pass for this one's.
	for name in os.listdir(args.dir):
		if name.endswith(".png"):
			os.remove(os.path.join(args.dir, name))
	ini = os.path.join(args.dir, "network.ini")
	with open(ini, "w") as f:
		f.write("[Network]\nproAdhocServer = 127.0.0.1\nAdhocServerRelayMode = %d\n" % (0 if args.p2p else 1))
	names = [name.strip() for name in args.names.split(",")] if args.names else []
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
		cmd.append("--nickname=" + (names[n - 1] if n <= len(names) else "PPSSPP %d" % n))
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
	for t in range(args.times):
		if t > 0:
			time.sleep(args.every)
		for inst in selected(session, args.i):
			dbg = Debugger(inst["port"])
			# The answer only comes once the press is over, so don't wait for it.
			dbg.send("input.buttons.press", button=args.button, duration=args.frames)
			dbg.close()


# The system on-screen keyboard's first page (Core/Dialog/PSPOskConstants.cpp, Latin lowercase).
OSK_ROWS = ["1234567890-+", "qwertyuiop[]", "asdfghjkl;@~", "zxcvbnm,./?\\", "=<>'\u20ac\u00a5&\u00a7\u00a3*()"]


def cmd_type(args):
	# Types text into the system on-screen keyboard, which starts with the top left key selected, and
	# finishes with START unless --no-finish. Only the first page (lowercase, digits) and space.
	session = load_session(args)
	insts = selected(session, args.i)
	positions = {ch: (r, c) for r, row in enumerate(OSK_ROWS) for c, ch in enumerate(row)}
	for ch in args.text:
		if ch != " " and ch not in positions:
			sys.exit("type: %r isn't on the keyboard's first page" % ch)

	def press(button):
		for inst in insts:
			dbg = Debugger(inst["port"])
			dbg.send("input.buttons.press", button=button, duration=3)
			dbg.close()
		time.sleep(0.15)

	row, col = 0, 0
	for ch in args.text:
		if ch == " ":
			press("square")
			continue
		r, c = positions[ch]
		while row < r:
			press("down")
			row += 1
		while row > r:
			press("up")
			row -= 1
		while col < c:
			press("right")
			col += 1
		while col > c:
			press("left")
			col -= 1
		press("cross")
	if not args.no_finish:
		press("start")


def cmd_stick(args):
	# Hold the left stick at X, Y (-1 to 1, Y up) for a while, then let it go.
	session = load_session(args)
	insts = selected(session, args.i)
	for inst in insts:
		dbg = Debugger(inst["port"])
		dbg.request("input.analog.send", x=args.x, y=args.y)
		dbg.close()
	time.sleep(args.seconds)
	for inst in insts:
		dbg = Debugger(inst["port"])
		dbg.request("input.analog.send", x=0, y=0)
		dbg.close()


def cmd_wait(args):
	time.sleep(args.seconds)


def grab_png(inst, attempts=3):
	# An instance busy loading can be slow to answer; try again rather than end a long script.
	for attempt in range(attempts):
		try:
			dbg = Debugger(inst["port"])
			dbg.stop_cpu()
			answer = dbg.request("gpu.buffer.screenshot", type="uri")
			dbg.resume_cpu()
			dbg.close()
			return base64.b64decode(answer["uri"].split(",", 1)[1])
		except (TimeoutError, socket.timeout):
			if attempt == attempts - 1:
				raise


def cmd_shot(args):
	session = load_session(args)
	files = []
	for inst in selected(session, args.i):
		path = os.path.join(args.dir, "%s_%d.png" % (args.name, inst["n"]))
		with open(path, "wb") as f:
			f.write(grab_png(inst))
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


def parse_ints(text, count):
	values = [int(v) for v in text.split(",")]
	if len(values) != count:
		sys.exit("Expected %d comma-separated numbers, got %r" % (count, text))
	return values


def script_path(args, path):
	# Paths in a script are relative to the script.
	return path if os.path.isabs(path) else os.path.join(getattr(args, "base", "."), path)


def screen_matches(args, image):
	from PIL import Image, ImageChops, ImageStat
	if args.pixel:
		x, y = parse_ints(args.pixel, 2)
		want = bytes.fromhex(args.color)
		have = image.convert("RGB").getpixel((x, y))
		return max(abs(a - b) for a, b in zip(have, want)) <= args.tolerance
	ref = Image.open(script_path(args, args.ref)).convert("RGB")
	x, y = parse_ints(args.box, 2)[:2] if args.box else (0, 0)
	region = image.convert("RGB").crop((x, y, x + ref.width, y + ref.height))
	diff = ImageStat.Stat(ImageChops.difference(region, ref)).mean
	return sum(diff) / len(diff) <= args.tolerance


def cmd_until(args):
	# Waits until every selected instance shows something, optionally pressing a button meanwhile.
	import io
	from PIL import Image
	if not args.pixel and not args.ref:
		sys.exit("until needs a reference image or --pixel")
	session = load_session(args)
	waiting = list(selected(session, args.i))
	end = time.time() + args.timeout
	last_press = 0.0
	while waiting:
		if time.time() > end:
			for inst in waiting:
				path = os.path.join(args.dir, "until_failed_%d.png" % inst["n"])
				with open(path, "wb") as f:
					f.write(grab_png(inst))
			sys.exit("until: instances %s never matched; their screens are in until_failed_N.png" % ",".join(str(i["n"]) for i in waiting))
		if args.press and time.time() - last_press >= args.every:
			for inst in waiting:
				dbg = Debugger(inst["port"])
				dbg.send("input.buttons.press", button=args.press, duration=6)
				dbg.close()
			last_press = time.time()
		waiting = [inst for inst in waiting if not screen_matches(args, Image.open(io.BytesIO(grab_png(inst))))]
		if waiting:
			time.sleep(args.poll)


def cmd_crop(args):
	from PIL import Image
	x, y, w, h = parse_ints(args.box, 4)
	Image.open(args.image).crop((x, y, x + w, y + h)).save(args.out)
	print("%s: %dx%d at %d,%d (use --box %d,%d)" % (args.out, w, h, x, y, x, y))


def cmd_log(args):
	if args.level == "verbose":
		# MAX_LOGLEVEL in Common/Log.h compiles VERBOSE_LOG out of every build unless edited.
		print("log: verbose messages are compiled out (MAX_LOGLEVEL in Common/Log.h); this shows debug and above", file=sys.stderr)
	session = load_session(args)
	for inst in selected(session, args.i):
		dbg = Debugger(inst["port"])
		dbg.request("log.channel.set", channel=args.channel, level=args.level, enabled=True)
		dbg.close()


def cmd_watch(args):
	# Log messages only go to debugger clients that are connected, so collect them for a while.
	session = load_session(args)

	def collect(inst):
		path = os.path.join(args.dir, "%s_%d.log" % (args.name, inst["n"]))
		dbg = Debugger(inst["port"], logs=True)
		end = time.time() + args.seconds
		with open(path, "w") as f:
			while time.time() < end:
				try:
					message = dbg.wait_for(lambda m: m.get("event") == "log", timeout=max(0.1, end - time.time()))
				except (TimeoutError, socket.timeout):
					break
				f.write("%s %s%s\n" % (message.get("timestamp", ""), message.get("header", ""), message.get("message", "").rstrip("\n")))
		dbg.close()
		print(path)

	threads = [threading.Thread(target=collect, args=(inst,)) for inst in selected(session, args.i)]
	for t in threads:
		t.start()
	for t in threads:
		t.join()


COMMANDS = {}


def cmd_run(args):
	with open(args.script) as f:
		for line in f:
			line = line.split("#", 1)[0].strip()
			if not line:
				continue
			print("> " + line)
			sub = build_parser().parse_args(["--dir", args.dir] + line.split())
			sub.base = os.path.dirname(os.path.abspath(args.script))
			COMMANDS[sub.command](sub)


def build_parser():
	parser = argparse.ArgumentParser(description="Run and drive several PPSSPP instances over ad hoc")
	parser.add_argument("--dir", default=DEFAULT_DIR, help="Where the session keeps its memory sticks, logs and screenshots")
	sub = parser.add_subparsers(dest="command", required=True)

	p = sub.add_parser("start", help="Start N instances of a game")
	p.add_argument("game")
	p.add_argument("-n", type=int, default=2, help="Number of instances (default 2)")
	p.add_argument("--p2p", action="store_true", help="Talk directly instead of through the relay (needs a loopback address per instance)")
	p.add_argument("--names", help="Comma-separated nicknames for the instances (default: PPSSPP 1, PPSSPP 2, ...)")
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
	p.add_argument("--times", type=int, default=1, help="Press it this many times (for menus that need several)")
	p.add_argument("--every", type=float, default=2, help="With --times: seconds between presses (default 2)")

	p = sub.add_parser("type", help="Type text into the system on-screen keyboard, just opened")
	p.add_argument("text")
	p.add_argument("-i", help="Instances, like 1,2 (default all)")
	p.add_argument("--no-finish", action="store_true", help="Don't press START at the end")

	p = sub.add_parser("stick", help="Hold the left analog stick")
	p.add_argument("x", type=float, help="-1 (left) to 1 (right)")
	p.add_argument("y", type=float, help="-1 (down) to 1 (up)")
	p.add_argument("-i", help="Instances, like 1,2 (default all)")
	p.add_argument("--seconds", type=float, default=1, help="How long to hold it (default 1)")

	p = sub.add_parser("wait", help="Wait (wall clock)")
	p.add_argument("seconds", type=float)

	p = sub.add_parser("shot", help="Screenshot each instance, and all side by side as NAME.png")
	p.add_argument("name")
	p.add_argument("-i")

	p = sub.add_parser("stats", help="Emulated time and frame rate of each instance")
	p.add_argument("-i")

	p = sub.add_parser("until", help="Wait until the screen shows something: a region like a reference image, or a pixel's color")
	p.add_argument("ref", nargs="?", help="Reference image (a crop of a screenshot, see crop); relative to the script in a script")
	p.add_argument("--box", help="X,Y where the reference image goes on the screen (default 0,0)")
	p.add_argument("--pixel", help="X,Y of a pixel to check instead")
	p.add_argument("--color", help="With --pixel: RRGGBB")
	p.add_argument("--tolerance", type=float, default=12, help="Mean difference per channel allowed (0-255; default 12)")
	p.add_argument("--press", help="Keep pressing this button while waiting")
	p.add_argument("--every", type=float, default=2, help="With --press: seconds between presses (default 2)")
	p.add_argument("--timeout", type=float, default=60)
	p.add_argument("--poll", type=float, default=0.5, help="Seconds between screenshots")
	p.add_argument("-i")

	p = sub.add_parser("crop", help="Cut a reference image for until out of a screenshot")
	p.add_argument("image")
	p.add_argument("box", help="X,Y,W,H")
	p.add_argument("out")

	p = sub.add_parser("log", help="Set a log channel's level, like sceNet debug (see watch)")
	p.add_argument("channel")
	p.add_argument("level", help="notice, error, warning, info, debug or verbose")
	p.add_argument("-i")

	p = sub.add_parser("watch", help="Collect each instance's log messages for a while into NAME_N.log")
	p.add_argument("seconds", type=float)
	p.add_argument("name")
	p.add_argument("-i")

	p = sub.add_parser("run", help="Run a script of these commands, one per line")
	p.add_argument("script")

	sub.add_parser("stop", help="Stop the instances")
	return parser


COMMANDS.update(start=cmd_start, stop=cmd_stop, press=cmd_press, stick=cmd_stick, type=cmd_type, wait=cmd_wait, shot=cmd_shot, stats=cmd_stats, until=cmd_until, crop=cmd_crop, log=cmd_log,
	watch=cmd_watch, run=cmd_run)


def main():
	args = build_parser().parse_args()
	COMMANDS[args.command](args)


if __name__ == "__main__":
	main()
