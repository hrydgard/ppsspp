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
  "cpu/fpu/fpu",
  "cpu/icache/icache",
  "cpu/lsu/lsu",
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
  tests_passed = 0
  tests_failed = 0
  
  flags = ""

  for test in test_list:
    elf_filename = TEST_ROOT + test + ".elf"
    expected_filename = TEST_ROOT + test + ".expected"
    if not os.path.exists(expected_filename):
      print("WARNING: expects file missing, failing test: " + expected_filename)
      tests_failed += 1
      continue

    expected_output = open(expected_filename).read()

    cmdline = PPSSPP_EXE + " " + test + ".elf"
    proc = subprocess.Popen(cmdline, stdin=subprocess.PIPE, stdout=subprocess.PIPE)
    
    output = proc.stdout.read()
    print output
    if output == expected_output:
      print "Test passed!"
    else:
      print "Test failed ==============  output:"
      print output
      print "============== expected output:"
      print expected_output

    tests_passed += 1

  print "%i tests passed, %i tests failed" % (tests_passed, tests_failed)


def main():
  init()
  if len(sys.argv) == 1:
    run_tests(tests_good)

main()
