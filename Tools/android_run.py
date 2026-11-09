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

"""Starts PPSSPP on an Android device, with command line options, optionally building and installing it first.

    python Tools/android_run.py --build --start-screen=memstick   # build, install, open the memory stick screen
    python Tools/android_run.py --log --start-screen=firmware     # follow the app's log until Ctrl+C
    python Tools/android_run.py -- --some-option=x                # anything after -- goes to the app as is

The app only reads its options at startup, so it's force-stopped first. --start-screen names are listed in
NativeInit() in UI/NativeApp.cpp.
"""

import argparse
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PACKAGE = "org.ppsspp.ppsspp"
APK = os.path.join(ROOT, "android", "build", "outputs", "apk", "normal", "debug", "android-normal-debug.apk")


def adb(args, device, **kwargs):
	return subprocess.run(["adb"] + (["-s", device] if device else []) + args, **kwargs)


def build_and_install(device):
	gradlew = os.path.join(ROOT, "gradlew.bat" if os.name == "nt" else "gradlew")
	if subprocess.run([gradlew, ":android:assembleNormalDebug"], cwd=ROOT).returncode != 0:
		print("Build failed")
		return False
	# -d: a branch can have a lower versionCode (the commit count) than what's installed. See
	# docs/android-device-testing.md, and don't trust an install without checking what's there afterwards.
	result = adb(["install", "-r", "-d", "-t", APK], device, capture_output=True, text=True)
	print(result.stdout.strip(), result.stderr.strip())
	if result.returncode != 0 or "Success" not in result.stdout:
		print("Install failed")
		return False
	info = adb(["shell", "dumpsys package %s | grep versionName" % PACKAGE], device, capture_output=True, text=True).stdout
	print("Installed: " + info.strip())
	return True


def start(device, app_args):
	"""Force-stops the app and starts it with app_args (a list of options)."""
	adb(["shell", "am force-stop " + PACKAGE], device)
	# Some devices default every log tag to silent (persist.log.tag=S), which drops all of ours before it
	# reaches logcat. This lets ours through, until the next reboot.
	adb(["shell", "setprop log.tag.PPSSPP V"], device)
	command = "am start -n %s/.PpssppActivity" % PACKAGE
	if app_args:
		# The shell on the device parses this once more, so quote the whole thing.
		joined = " ".join(app_args).replace("'", "'\\''")
		command += " --es %s.Args '%s'" % (PACKAGE, joined)
	return adb(["shell", command], device, capture_output=True, text=True)


def main():
	parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
	parser.add_argument("--build", action="store_true", help="build and install the normal debug APK first")
	parser.add_argument("-s", "--device", help="adb device serial, if there's more than one")
	parser.add_argument("--log", action="store_true", help="follow the app's log (the PPSSPP tag) until Ctrl+C")
	parser.add_argument("--start-screen", help="the screen to start on, like memstick, memsticksetup, firmware or storagetest")
	parser.add_argument("app_args", nargs="*", help="more options for the app, after --")
	args = parser.parse_args()

	if args.build and not build_and_install(args.device):
		return 1

	app_args = list(args.app_args)
	if args.start_screen:
		app_args.insert(0, "--start-screen=" + args.start_screen)
	if args.log:
		adb(["logcat", "-c"], args.device)
	result = start(args.device, app_args)
	if result.returncode != 0 or "Error" in result.stdout:
		print(result.stdout, result.stderr)
		return 1
	print("Started %s %s" % (PACKAGE, " ".join(app_args)))
	if args.log:
		try:
			adb(["logcat", "-v", "brief", "PPSSPP:V", "*:S"], args.device)
		except KeyboardInterrupt:
			pass
	return 0


if __name__ == "__main__":
	sys.exit(main())
