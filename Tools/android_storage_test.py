#!/usr/bin/env python3
# Copyright (c) 2012- PPSSPP Project.

# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, version 2.0 or later versions.

# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License 2.0 for more details.

# A copy of the GPL 2.0 should have been included with the program.
# If not, see http://www.gnu.org/licenses/

# Official git repository and contact information can be found at
# https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

"""Runs the storage tests (UI/StorageTestScreen.cpp) in the app on an Android device.

The unit tests can't reach the content URI storage code, since they have no Java VM, so these run in the
app itself, in a folder picked with the system folder picker. The first time, create a new empty folder
in the picker on the device and choose it. The app remembers it (in its private data), so later runs
need no hands.

    python Tools/android_storage_test.py --build     # build and install the normal debug APK first
    python Tools/android_storage_test.py             # run with what's installed

Exits non-zero if a test failed, or no results came within --timeout seconds.
"""

import argparse
import re
import subprocess
import sys
import threading
import time

# The build, install and start steps are shared with android_run.py, next to this.
from android_run import PACKAGE, adb, build_and_install, start


def main():
	parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
	parser.add_argument("--build", action="store_true", help="build and install the normal debug APK first")
	parser.add_argument("-s", "--device", help="adb device serial, if there's more than one")
	parser.add_argument("--timeout", type=int, default=300, help="seconds to wait for the results, including picking a folder (default 300)")
	args = parser.parse_args()

	if args.build and not build_and_install(args.device):
		return 1

	adb(["logcat", "-c"], args.device)
	logcat = subprocess.Popen(["adb"] + (["-s", args.device] if args.device else []) + ["logcat", "-v", "brief", "PPSSPP:V", "*:S"],
		stdout=subprocess.PIPE, text=True, errors="replace")
	# A fresh start, so the start screen applies (it's only read at startup).
	if start(args.device, ["--start-screen=storagetest"]).returncode != 0:
		print("Couldn't start " + PACKAGE)
		logcat.kill()
		return 1

	# Read on a thread, so the timeout works even when logcat is quiet.
	lines = []
	done = threading.Event()

	def reader():
		for line in logcat.stdout:
			lines.append(line.rstrip())
			if "StorageTest: DONE" in line:
				done.set()
				return

	threading.Thread(target=reader, daemon=True).start()
	deadline = time.time() + args.timeout
	shown = 0
	asked = False
	while not done.is_set() and time.time() < deadline:
		time.sleep(0.2)
		while shown < len(lines):
			line = lines[shown]
			shown += 1
			if "StorageTest:" in line:
				print(line[line.index("StorageTest:"):])
		if not asked and time.time() > deadline - args.timeout + 5 and not any("StorageTest:" in l for l in lines):
			print("No results yet. If this is the first run, tap Choose folder on the device, create a new empty folder in the picker, and choose it.")
			asked = True
	logcat.kill()
	for line in lines[shown:]:
		if "StorageTest:" in line:
			print(line[line.index("StorageTest:"):])

	summary = [l for l in lines if "StorageTest: DONE" in l]
	if not summary:
		print("Timed out after %d seconds" % args.timeout)
		return 1
	failed = int(re.search(r"DONE, (\d+) failed", summary[-1]).group(1))
	return 1 if failed else 0


if __name__ == "__main__":
	sys.exit(main())
