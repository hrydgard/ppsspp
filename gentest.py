# Utility to make it slightly easier to get the results from a test back
# and put it as an "expected" file.
# I can't seem to get pspsh to automatically wait for the psp app to exit,
# unfortunately.

import sys
import io
import os
import subprocess
import shutil
import time

PSPSH = "pspsh"
TEST_ROOT = "pspautotests/tests/"
PORT = 3000
TEST = "cpu/cpu/cpu"
OUTFILE = "__testoutput.txt"
OUTFILE2 = "__testerror.txt"


tests_to_generate = [
  "cpu/cpu/cpu",
  "cpu/icache/icache",
  "cpu/lsu/lsu",
  "cpu/fpu/fpu",
]


def gen_test(test):
  print("Running test " + test + " on the PSP...")
  
  if os.path.exists(OUTFILE):
    os.unlink(OUTFILE)
  if os.path.exists(OUTFILE2):
    os.unlink(OUTFILE2)

  prx_path = TEST_ROOT + test + ".prx"
  expected_path = TEST_ROOT + test + ".expected"

  if not os.path.exists(prx_path):
    print "You must compile the test into a PRX first (" + prx_path + ")"
    return

  # First, write a command file for PSPSH

  f = open("cmdfile.txt", "w")
  f.write(prx_path)
  f.close()

  os.system("pspsh -p %i cmdfile.txt" % (PORT,))
  
  # Allow the test a second to execute - TODO: tweak
  time.sleep(1)

  if os.path.exists(OUTFILE):
    # Should check for size as well...
    shutil.copyfile(OUTFILE, expected_path)
    print "Expected file written: " + expected_path
  else:
    print "ERROR: No " + OUTFILE + " was written, can't write .expected"

  os.unlink("cmdfile.txt")

  # Allow the system a couple of seconds to reconnect to the PSP
  time.sleep(2)

def main():
  args = sys.argv[1:]
  
  tests = tests_to_generate
  if len(args):
    tests = args

  for test in tests:
    gen_test(test)

main()
