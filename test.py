# Automated script to run the pspautotests test suite in PPSSPP.

import sys
import io
import os
import subprocess


PPSSPP_EXECUTABLES = [ "Windows\\Release\\PPSSPPHeadless.exe", "SDL/build/ppsspp-headless" ]
PPSSPP_EXE = None
TEST_ROOT = "pspautotests/tests/"

# Test names are the C files without the .c extension.
# These have worked and should keep working always - regression tests.
tests_good = [
  "cpu/cpu/cpu",
  "cpu/vfpu/vfpu",
  "cpu/vfpu/convert/vfpu_convert",
  "cpu/vfpu/prefixes/vfpu_prefixes",
  "cpu/vfpu/colors/vfpu_colors",
  "cpu/icache/icache",
  "cpu/lsu/lsu",
  "cpu/fpu/fpu",

  "display/display",
  "dmac/dmactest",
  "intr/intr",
  "intr/vblank/vblank",
  "misc/testgp",
  "string/string",
  "gpu/callbacks/ge_callbacks",
  "threads/mbx/mbx",
  "rtc/rtc",
]

# These are the next tests up for fixing.
tests_next = [
  "ctrl/ctrl",
  "gpu/simple/simple",
  "gpu/triangle/triangle",
  "hle/check_not_used_uids",
  "font/fonttest",
  "io/cwd/cwd",
  "io/directory/directory",
  "io/io/io",
  "io/iodrv/iodrv",
  "malloc/malloc",
  "modules/loadexec/loader",
  "power/power",
  "threads/events/events",
  "threads/fpl/fpl",
  "threads/msgpipe/msgpipe",
  "threads/mutex/mutex",
  "threads/scheduling/scheduling",
  "threads/semaphores/semaphores",
  "threads/threads/threads",
  "threads/vpl/vpl",
  "threads/vtimers/vtimers",
  "threads/wakeup/wakeup",
  "umd/callbacks/umd",
  "umd/io/umd_io",
  "umd/raw_access/raw_acess",
  "utility/systemparam",
  "video/pmf",
  "video/pmf_simple",
]

# These don't even run (or run correctly) on the real PSP
test_broken = [
  "sysmem/sysmem",
  "mstick/mstick",
]


# These are the tests we ignore (not important, or impossible to run)
tests_ignored = [
  "kirk/kirk",
  "me/me",

  "umd/umd", # mostly fixed but output seems broken? (first retval of unregister...)
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



def run_tests(test_list, args):
  global PPSSPP_EXE
  tests_passed = []
  tests_failed = []
  
  flags = ""

  for test in test_list:
    # Try prx first
    expected_filename = TEST_ROOT + test + ".expected"

    elf_filename = TEST_ROOT + test + ".prx"
    print elf_filename

    if not os.path.exists(elf_filename):
      print "WARNING: no prx, trying elf"
      elf_filename = TEST_ROOT + test + ".elf"

    if not os.path.exists(elf_filename):
      print("ERROR: PRX/ELF file missing, failing test: " + test)
      tests_failed.append(test)
      continue

    if not os.path.exists(expected_filename):
      print("WARNING: expects file missing, failing test: " + test)
      tests_failed.append(test)
      continue

    expected_output = open(expected_filename).read()

    cmdline = PPSSPP_EXE + " " + elf_filename + " " + " ".join(args)
    #print "Cmdline: " + cmdline
    proc = subprocess.Popen(cmdline, stdin=subprocess.PIPE, stdout=subprocess.PIPE, shell=True)

    output = proc.stdout.read().strip()
    if output.startswith("TESTERROR"):
      print "Failed to run test " + elf_filename + "!"
      tests_failed.append(test)
      continue

    different = False
    expected_lines = expected_output.splitlines()
    output_lines = output.splitlines()
    
    for i in range(0, min(len(output_lines), len(expected_lines))):
      if output_lines[i] != expected_lines[i]:
        print "%i < %s" % (i, output_lines[i])
        print "%i > %s" % (i, expected_lines[i])
        different = True

    if len(output_lines) != len(expected_lines):
      print "*** Different number of lines!"
      different = True

    if not different:
      print "  " + test + " - passed!"
      tests_passed.append(test)
    else:
      #print "============== output from failed " + test + " :"
      #print output
      #print "============== expected output:"
      #print expected_output
      #print "==============================="
      tests_failed.append(test)

  print "%i tests passed, %i tests failed." % (
      len(tests_passed), len(tests_failed))

  if len(tests_failed):
    print "Failed tests:"
    for t in tests_failed:
      print "  " + t
  print "Ran " + PPSSPP_EXE


def main():
  init()
  tests = []
  args = []
  for arg in sys.argv[1:]:
    if arg[0] == '-':
      args.append(arg)
    else:
      tests.append(arg)

  if not tests:
    tests = tests_good

  run_tests(tests, args)

main()
