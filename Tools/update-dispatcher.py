#!/usr/bin/env python

# Regenerates Core/MIPS/InterpreterDispatch.cpp (the ExecInstruction switch-tree
# interpreter dispatcher) from the tables in Core/MIPS/MIPSTables.cpp.
#
# Run this after changing anything that affects those tables - new instructions,
# retimed cycle counts, encoding changes, etc. The generated file is checked into
# git like any other source file, not built on the fly, so it needs to be
# regenerated and the diff committed whenever the tables change.
#
# This script only needs an already-built PPSSPPHeadless binary (see
# docs/pspautotests.md for how to build one); it doesn't build anything itself.
# After it finishes, just build normally - Core/MIPS/InterpreterDispatch.cpp is
# already wired into every build system, so no further steps are needed.

import os
import subprocess
import sys

# Same search list test.py uses to find a headless build.
CANDIDATES = [
  "Windows/x64/Debug/PPSSPPHeadless.exe",
  "Windows/x64/Release/PPSSPPHeadless.exe",
  "Windows/Debug/PPSSPPHeadless.exe",
  "Windows/Release/PPSSPPHeadless.exe",
  "build/Debug/PPSSPPHeadless",
  "build/Release/PPSSPPHeadless",
  "build/RelWithDebInfo/PPSSPPHeadless",
  "build/MinSizeRel/PPSSPPHeadless",
  "build/PPSSPPHeadless",
  "./PPSSPPHeadless",
  "./PPSSPPHeadless.exe",
]

OUT_PATH = os.path.join("Core", "MIPS", "InterpreterDispatch.cpp")


def find_headless():
  for candidate in CANDIDATES:
    if os.path.isfile(candidate):
      return candidate
  return None


def main():
  repo_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
  os.chdir(repo_root)

  headless = find_headless()
  if headless is None:
    sys.stderr.write("error: couldn't find a built PPSSPPHeadless binary. Checked:\n")
    for candidate in CANDIDATES:
      sys.stderr.write("  %s\n" % candidate)
    sys.stderr.write("Build PPSSPPHeadless first (see docs/pspautotests.md), then re-run this script.\n")
    return 1

  print("Using %s" % headless)

  # subprocess/CreateProcess on Windows can fail to resolve a relative,
  # forward-slash path as the executable itself - use an absolute, normalized path.
  headless_abs = os.path.normpath(os.path.abspath(headless))

  try:
    result = subprocess.run([headless_abs, "--generate-interpreter-dispatch"], capture_output=True)
  except OSError as e:
    sys.stderr.write("error: couldn't run %s: %s\n" % (headless, e))
    return 1

  if result.returncode != 0:
    sys.stderr.buffer.write(result.stderr)
    sys.stderr.write("error: %s exited with code %d\n" % (headless, result.returncode))
    return 1

  generated = result.stdout

  # Sanity check before clobbering the tracked file - a stale/broken binary should
  # fail loudly here rather than silently truncating InterpreterDispatch.cpp.
  if b"int ExecInstruction(MIPSState *mips, MIPSOpcode op)" not in generated:
    sys.stderr.write("error: generated output doesn't look right (missing ExecInstruction) - not overwriting %s\n" % OUT_PATH)
    return 1

  with open(OUT_PATH, "wb") as f:
    f.write(generated)

  print("Regenerated %s" % OUT_PATH)
  return 0


if __name__ == "__main__":
  sys.exit(main())
