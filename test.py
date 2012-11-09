# Automated script to run the pspautotests test suite in PPSSPP.

import sys
import io
import os
import subprocess


PPSSPP_EXECUTABLES = [ "Windows/Release/PPSSPPHeadless.exe", "SDL/build/ppsspp_headless" ]
PPSSPP_EXE = None
TEST_ROOT = "pspautotests/tests/"

# Test names are the C files without the .c extension.
# These have worked and should keep working always - regression tests.
tests_good = [
  "cpu/cpu/cpu",
  "cpu/icache/icache",
  "cpu/lsu/lsu",
  "cpu/fpu/fpu",
]

# These are the next tests up for fixing.
tests_next = [
]

# These are the tests we ignore (not important, or impossible to run)
tests_ignored = [

]



def init():
  global PPSSPP_EXE
  if not os.path.exists("pspautotests"):
    print "Please run git submodule init; git submodule update;"
    sys.exit(1)

  if not os.path.exists(TEST_ROOT + "cpu/cpu/cpu.elf"):
    print "Please install the pspsdk and run build.sh or build.bat in pspautotests/tests"
    sys.exit(1)

  for p in PPSSPP_EXECUTABLES:
    if os.path.exists(p):
      PPSSPP_EXE = p
      break

  if not PPSSPP_EXE:
    print "PPSSPP executable missing, please build one."
    sys.exit(1)



def run_tests(test_list):
  global PPSSPP_EXE
  tests_passed = []
  tests_failed = []
  
  flags = ""

  for test in test_list:
    elf_filename = TEST_ROOT + test + ".elf"
    expected_filename = TEST_ROOT + test + ".expected"
    if not os.path.exists(expected_filename):
      print("WARNING: expects file missing, failing test: " + expected_filename)
      tests_failed.append(test)
      continue

    expected_output = open(expected_filename).read()

    cmdline = PPSSPP_EXE + " " + elf_filename
    #print "Cmdline: " + cmdline
    proc = subprocess.Popen(cmdline, stdin=subprocess.PIPE, stdout=subprocess.PIPE)

    output = proc.stdout.read().strip()
    if output.startswith("TESTERROR"):
      print "Failed to run test " + elf_filename + "!"
      tests_failed.append(test)
      continue

    different = False
    expected_lines = expected_output.splitlines()
    output_lines = output.splitlines()
    
    for i in range(0, len(expected_lines)):
      if output_lines[i] != expected_lines[i]:
        print "First different line (output vs expected):"
        print output_lines[i]
        print " --- expected: ---"
        print expected_lines[i]
        different = True

    if not different:
      print "  " + test + " - passed!"
      tests_passed.append(test)
    else:
      print test + " failed ============== output:"
      print output
      print "============== expected output:"
      print expected_output
      print "==============================="
      tests_failed.append(test)

  print "%i tests passed, %i tests failed" % (len(tests_passed), len(tests_failed))
  if len(tests_failed):
    print "Failed tests:"
    for t in tests_failed:
      print "  " + t
  print "Ran " + PPSSPP_EXE

def main():
  init()
  if len(sys.argv) == 1:
    run_tests(tests_good)

main()
