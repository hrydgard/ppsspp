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

"""Runs the C++ unit tests on an Android device over adb.

Builds ppsspp_unittest with the legacy NDK build (android/jni) if asked to, pushes it and the files
some tests read to /data/local/tmp, and runs each test as its own process, so that one that crashes
can't take the others' results with it. Every test's output is kept in --log-dir.

    python Tools/android_unittest.py --build              # build for arm64-v8a, then run everything
    python Tools/android_unittest.py Path VFS             # just these, with the binary already built
    python Tools/android_unittest.py -s R5CNC19P5DP all   # pick a device

The build needs the NDK: android/ab.cmd has its path on Windows, and android/ab.sh reads $NDK.
Exits non-zero if any test failed.
"""

import argparse
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEVICE_DIR = "/data/local/tmp/ppsspp_unittest_dir"
# Files the tests open, relative to the working directory (see e.g. unittest/TestVFS.cpp).
DATA_FILES = ["source_assets/ziptest.zip"]


def adb(args, device, **kwargs):
	cmd = ["adb"] + (["-s", device] if device else []) + args
	return subprocess.run(cmd, **kwargs)


def shell(command, device, **kwargs):
	return adb(["shell", command], device, **kwargs)


def build(abi):
	android_dir = os.path.join(ROOT, "android")
	if os.name == "nt":
		cmd = ["cmd", "/c", os.path.join(android_dir, "ab.cmd"), "APP_ABI=" + abi, "UNITTEST=1"]
	else:
		cmd = ["./ab.sh", "APP_ABI=" + abi, "UNITTEST=1"]
	print("Building: " + " ".join(cmd))
	return subprocess.run(cmd, cwd=android_dir).returncode == 0


def list_tests(device):
	# Asking for a test that doesn't exist prints the list of the ones that do.
	out = shell("cd %s && ./ppsspp_unittest no_such_test 2>&1" % DEVICE_DIR, device, capture_output=True, text=True).stdout
	return [line.strip()[2:] for line in out.splitlines() if line.strip().startswith("* ")]


def main():
	parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
	parser.add_argument("tests", nargs="*", help="test names, or all (the default)")
	parser.add_argument("--build", action="store_true", help="build ppsspp_unittest first")
	parser.add_argument("--abi", default="arm64-v8a", help="the ABI to build and run (default arm64-v8a)")
	parser.add_argument("-s", "--device", help="adb device serial, if there's more than one")
	parser.add_argument("--log-dir", default=os.path.join(ROOT, "android", "unittest_logs"), help="where each test's output goes")
	args = parser.parse_args()

	if args.build and not build(args.abi):
		print("Build failed")
		return 1

	binary = os.path.join(ROOT, "android", "libs", args.abi, "ppsspp_unittest")
	if not os.path.exists(binary):
		print("No %s, build it with --build" % binary)
		return 1

	# Push everything into a directory of our own, which is also the working directory of the tests.
	shell("mkdir -p %s/source_assets" % DEVICE_DIR, args.device, check=True)
	if adb(["push", binary, DEVICE_DIR + "/ppsspp_unittest"], args.device).returncode != 0:
		return 1
	for f in DATA_FILES:
		adb(["push", os.path.join(ROOT, f), DEVICE_DIR + "/" + f], args.device, check=True, capture_output=True)
	# Pushing over the old binary drops the executable bit.
	shell("chmod 755 %s/ppsspp_unittest" % DEVICE_DIR, args.device, check=True)

	tests = args.tests if args.tests and args.tests != ["all"] else list_tests(args.device)
	if not tests:
		print("Found no tests to run")
		return 1

	os.makedirs(args.log_dir, exist_ok=True)
	failed = []
	for test in tests:
		result = shell("cd %s && ./ppsspp_unittest %s 2>&1" % (DEVICE_DIR, test), args.device, capture_output=True, text=True, errors="replace")
		with open(os.path.join(args.log_dir, test + ".log"), "w", encoding="utf-8") as f:
			f.write(result.stdout)
		code = result.returncode
		# adb passes the exit code through. 128 + signal for a crash, like 139 for SIGSEGV.
		status = "ok" if code == 0 else ("CRASHED (%d)" % code if code > 128 else "FAILED (%d)" % code)
		print("%-28s %s" % (test, status))
		if code != 0:
			failed.append(test)

	print()
	print("%d of %d passed. Logs in %s" % (len(tests) - len(failed), len(tests), args.log_dir))
	if failed:
		print("Failed: " + " ".join(failed))
		print("To run just those again: python Tools/android_unittest.py " + " ".join(failed))
	return 1 if failed else 0


if __name__ == "__main__":
	sys.exit(main())
