#!/usr/bin/env python
"""Small wrapper that locates the newest PPSSPPHeadless binary (same way
test.py does) and forwards all command line arguments to it.

Usage:
    python3 headless.py [OPTIONS] [FILES...]
"""

import glob
import os
import subprocess
import sys

PPSSPP_EXECUTABLES = [
    # Windows
    "Windows\\Debug\\PPSSPPHeadless.exe",
    "Windows\\Release\\PPSSPPHeadless.exe",
    "Windows\\x64\\Debug\\PPSSPPHeadless.exe",
    "Windows\\x64\\Release\\PPSSPPHeadless.exe",
    "build*/PPSSPPHeadless.exe",
    "./PPSSPPHeadless.exe",
    # Mac
    "build*/Debug/PPSSPPHeadless",
    "build*/Release/PPSSPPHeadless",
    "build*/RelWithDebInfo/PPSSPPHeadless",
    "build*/MinSizeRel/PPSSPPHeadless",
    # Linux
    "build*/PPSSPPHeadless",
    "./PPSSPPHeadless",
]


def find_headless():
    env_path = os.environ.get("PPSSPP_HEADLESS")
    if env_path and os.path.exists(env_path):
        return env_path
    possible_exes = [glob.glob(f) for f in PPSSPP_EXECUTABLES]
    possible_exes = [x for sublist in possible_exes for x in sublist]
    existing = [f for f in possible_exes if os.path.exists(f)]
    if not existing:
        return None
    return max(existing, key=os.path.getmtime)


def main():
    headless = find_headless()
    if headless is None:
        print("ERROR: PPSSPPHeadless binary not found. Set PPSSPP_HEADLESS or run from the repo root.", file=sys.stderr)
        return 2
    return subprocess.call([headless] + sys.argv[1:])


if __name__ == "__main__":
    sys.exit(main())
