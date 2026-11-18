#!/usr/bin/env python3
"""Run two real libretro cores through Netpacket using public PSP autotests (POSIX)."""

import argparse
from pathlib import Path
import re
import socket
import subprocess


def run(args, name):
	root = Path(__file__).resolve().parents[2]
	work = args.work_dir.resolve() / name
	work.mkdir(parents=True, exist_ok=True)
	assets = work / "system" / "PPSSPP"
	assets.parent.mkdir(exist_ok=True)
	if not assets.exists():
		assets.symlink_to(root / "assets", target_is_directory=True)
	sockets = socket.socketpair(socket.AF_UNIX, socket.SOCK_DGRAM)
	processes = []
	try:
		for i, role in enumerate(("host", "client")):
			saves = work / role
			saves.mkdir(exist_ok=True)
			program = root / "pspautotests/tests/net/adhoc/pair" / f"{name}_{role}.prx"
			log = (work / f"{role}.log").open("wb")
			sockets[i].setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, 4 * 1024 * 1024)
			command = [str(args.harness.resolve()), str(args.core.resolve()), str(program),
				str(assets.parent), str(saves), "pair", str(i), str(sockets[i].fileno())]
			processes.append((subprocess.Popen(command, pass_fds=(sockets[i].fileno(),),
				stdout=log, stderr=subprocess.STDOUT), log))
		for process, log in processes:
			if process.wait(timeout=40) != 0:
				raise RuntimeError(f"{name}: frontend failed; see {work}")
			log.close()
	finally:
		for process, log in processes:
			if process.poll() is None:
				process.kill()
				process.wait()
			log.close()
		for endpoint in sockets:
			endpoint.close()

	outputs = []
	for role in ("host", "client"):
		log = (work / f"{role}.log").read_text(errors="replace")
		if "PASS pair frontend" not in log:
			raise RuntimeError(f"{name}/{role}: frontend did not finish; see {work}")
		# PSP printf writes may arrive as multiple log messages. Remove only the
		# newline added by the frontend logger, preserving the program's newlines.
		fragments = re.findall(r"^\[sceIo\] (.*?)(?=^\[|^PASS pair|\Z)", log, re.M | re.S)
		output = "".join(fragment.removesuffix("\n") for fragment in fragments)
		(work / f"{role}.psp-output").write_text(output)
		outputs.append(output)
	common = ["Peers: 1"]
	if name == "basic":
		common += ["Same BSSID as the peer: yes", "data ok", "Leave group: 00000000"]
		required = [["Accept: ok", "Received 20000 bytes, ok", "Burst of 50: received at least 40, in order"],
			["Connect: 00000000", "Sent 20000 bytes", "Recv: 00000000, len 100, ok",
			 "Recv after the host closed: 8041070c", "Connect to a closed port: 80410718"]]
	else:
		common += ["2 members:", "Replica updates in 60 polls: 40 or more",
			"Peer's counter only increases: yes, magic right, timestamps increase", "ExitGameMode: 00000000"]
		required = [["CreateEnterGameMode: 00000000"], ["JoinEnterGameMode: 00000000"]]
	for role, output, extra in zip(("host", "client"), outputs, required):
		missing = [text for text in common + extra if text not in output]
		bad = re.search(r"\(sync \d+ timed out\)|data corrupt|bytes, corrupt|FAIL:", output)
		if missing or bad:
			raise RuntimeError(f"{name}/{role}: missing {missing}, error {bad.group(0) if bad else None}; see {work}")
		print(f"PASS {name}/{role}: real PSP networking through the libretro Netpacket callbacks", flush=True)


if __name__ == "__main__":
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("core", type=Path)
	parser.add_argument("--harness", type=Path, default=Path("build-netpacket-tests/NetpacketCoreSmoke"))
	parser.add_argument("--work-dir", type=Path, default=Path("build-netpacket-tests/pair-results"))
	parser.add_argument("--test", choices=("basic", "gamemode"), action="append")
	args = parser.parse_args()
	for name in args.test or ["basic", "gamemode"]:
		run(args, name)
