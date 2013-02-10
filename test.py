#!/usr/bin/env python
# Automated script to run the pspautotests test suite in PPSSPP.

import sys
import io
import os
import subprocess
import threading


PPSSPP_EXECUTABLES = [ "Windows\\Release\\PPSSPPHeadless.exe", "build/PPSSPPHeadless" ]
PPSSPP_EXE = None
TEST_ROOT = "pspautotests/tests/"
teamcity_mode = False
TIMEOUT = 5

class Command(object):
  def __init__(self, cmd):
    self.cmd = cmd
    self.process = None
    self.output = None
    self.timeout = False

  def run(self, timeout):
    def target():
      self.process = subprocess.Popen(self.cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE)
      self.output, _ = self.process.communicate()
      self.output = self.output.decode("utf-8")

    thread = threading.Thread(target=target)
    thread.start()

    thread.join(timeout)
    if thread.is_alive():
      self.timeout = True
      self.process.terminate()
      thread.join()

# Test names are the C files without the .c extension.
# These have worked and should keep working always - regression tests.
tests_good = [
  "cpu/cpu_alu/cpu_alu",
  "cpu/vfpu/base/vfpu",
  "cpu/vfpu/convert/vfpu_convert",
  "cpu/vfpu/prefixes/vfpu_prefixes",
  "cpu/vfpu/colors/vfpu_colors",
  "cpu/icache/icache",
  "cpu/lsu/lsu",
  "cpu/fpu/fpu",

  "ctrl/ctrl",
  "ctrl/idle/idle",
  "ctrl/sampling/sampling",
  "ctrl/sampling2/sampling2",
  "display/display",
  "dmac/dmactest",
  "loader/bss/bss",
  "intr/intr",
  "intr/vblank/vblank",
  "misc/testgp",
  "misc/libc",
  "misc/dcache",
  "string/string",
  "gpu/callbacks/ge_callbacks",
  "gpu/displaylist/state",
  "threads/alarm/alarm",
  "threads/alarm/cancel/cancel",
  "threads/alarm/refer/refer",
  "threads/alarm/set/set",
  "threads/events/events",
  "threads/events/cancel/cancel",
  "threads/events/clear/clear",
  "threads/events/create/create",
  "threads/events/delete/delete",
  "threads/events/poll/poll",
  "threads/events/refer/refer",
  "threads/events/set/set",
  "threads/events/wait/wait",
  "threads/k0/k0",
  "threads/lwmutex/create/create",
  "threads/lwmutex/delete/delete",
  "threads/lwmutex/lock/lock",
  "threads/lwmutex/priority/priority",
  "threads/lwmutex/try/try",
  "threads/lwmutex/try600/try600",
  "threads/lwmutex/unlock/unlock",
  "threads/mbx/mbx",
  "threads/mbx/cancel/cancel",
  "threads/mbx/create/create",
  "threads/mbx/delete/delete",
  "threads/mbx/poll/poll",
  "threads/mbx/priority/priority",
  "threads/mbx/receive/receive",
  "threads/mbx/refer/refer",
  "threads/mbx/send/send",
  "threads/mutex/mutex",
  "threads/mutex/create/create",
  "threads/mutex/delete/delete",
  "threads/mutex/lock/lock",
  "threads/mutex/priority/priority",
  "threads/mutex/try/try",
  "threads/mutex/unlock/unlock",
  "threads/semaphores/semaphores",
  "threads/semaphores/cancel/cancel",
  "threads/semaphores/create/create",
  "threads/semaphores/delete/delete",
  "threads/semaphores/poll/poll",
  "threads/semaphores/priority/priority",
  "threads/semaphores/refer/refer",
  "threads/semaphores/signal/signal",
  "threads/semaphores/wait/wait",
  "threads/vpl/vpl",
  "threads/vpl/delete",
  "threads/vpl/free",
  "threads/vpl/priority",
  "threads/vpl/refer",
  "threads/vpl/try",
  "power/power",
  "umd/callbacks/umd",
  "umd/wait/wait",
  "io/directory/directory",
]

tests_next = [
  "audio/sascore/sascore",
  "malloc/malloc",
# These are the next tests up for fixing. These run by default.
  "threads/fpl/fpl",
  "threads/k0/k0",
  "threads/msgpipe/msgpipe",
  "threads/scheduling/scheduling",
  "threads/threads/threads",
  "threads/vtimers/vtimer",
  "threads/vpl/allocate",
  "threads/vpl/create",
  "threads/wakeup/wakeup",
  "gpu/simple/simple",
  "gpu/triangle/triangle",
  "gpu/commands/basic",
  "hle/check_not_used_uids",
  "font/fonttest",
  "io/cwd/cwd",
  "io/io/io",
  "io/iodrv/iodrv",
  "modules/loadexec/loader",
  "rtc/rtc",
  "umd/io/umd_io",
  "umd/raw_access/raw_access",
  "utility/systemparam/systemparam",
  "video/pmf/pmf",
  "video/pmf_simple/pmf_simple",

  # Currently hang or crash.
  "audio/atrac/atractest",
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
    print("Please run git submodule init; git submodule update;")
    sys.exit(1)

  if not os.path.exists(TEST_ROOT + "cpu/cpu_alu/cpu_alu.prx"):
    print("Please install the pspsdk and run make in common/ and in all the tests")
    print("(checked for existence of cpu/cpu_alu/cpu_alu.prx)")
    sys.exit(1)

  for p in PPSSPP_EXECUTABLES:
    if os.path.exists(p):
      PPSSPP_EXE = p
      break

  if not PPSSPP_EXE:
    print("PPSSPP executable missing, please build one.")
    sys.exit(1)

def tcprint(arg):
  global teamcity_mode
  if teamcity_mode:
    print(arg)

def run_tests(test_list, args):
  global PPSSPP_EXE, TIMEOUT
  tests_passed = []
  tests_failed = []

  for test in test_list:
    # Try prx first
    expected_filename = TEST_ROOT + test + ".expected"

    elf_filename = TEST_ROOT + test + ".prx"
    print(elf_filename)

    if not os.path.exists(elf_filename):
      print("WARNING: no prx, trying elf")
      elf_filename = TEST_ROOT + test + ".elf"

    if not os.path.exists(elf_filename):
      print("ERROR: PRX/ELF file missing, failing test: " + test)
      tests_failed.append(test)
      tcprint("##teamcity[testIgnored name='%s' message='PRX/ELF missing']" % test)
      continue

    if not os.path.exists(expected_filename):
      print("WARNING: expects file missing, failing test: " + test)
      tests_failed.append(test)
      tcprint("##teamcity[testIgnored name='%s' message='Expects file missing']" % test)
      continue

    expected_output = open(expected_filename).read().strip()

    tcprint("##teamcity[testStarted name='%s' captureStandardOutput='true']" % test)

    cmdline = [PPSSPP_EXE, elf_filename]
    cmdline.extend([i for i in args if i not in ['-v', '-g']])
    if os.path.exists(expected_filename + ".bmp"):
      cmdline.extend(["--screenshot=" + expected_filename + ".bmp", "--graphics"])

    c = Command(cmdline)
    c.run(TIMEOUT)

    output = c.output.strip()

    if c.timeout:
      print(output)
      print("Test exceded limit of %d seconds." % TIMEOUT)
      tests_failed.append(test)
      tcprint("##teamcity[testFailed name='%s' message='Test timeout']" % test)
      tcprint("##teamcity[testFinished name='%s']" % test)
      continue

    if output.startswith("TESTERROR"):
      print("Failed to run test " + elf_filename + "!")
      tests_failed.append(test)
      tcprint("##teamcity[testFailed name='%s' message='Failed to run test']" % test)
      tcprint("##teamcity[testFinished name='%s']" % test)
      continue

    different = False
    expected_lines = [x.strip() for x in expected_output.splitlines()]
    output_lines = [x.strip() for x in output.splitlines()]

    for i in range(0, min(len(output_lines), len(expected_lines))):
      if output_lines[i] != expected_lines[i]:
        print("E%i < %s" % (i + 1, expected_lines[i]))
        print("O%i > %s" % (i + 1, output_lines[i]))
        different = True

    if len(output_lines) != len(expected_lines):
      for i in range(len(output_lines), len(expected_lines)):
        print("E%i < %s" % (i + 1, expected_lines[i]))
      for i in range(len(expected_lines), len(output_lines)):
        print("O%i > %s" % (i + 1, output_lines[i]))
      print("*** Different number of lines!")
      different = True

    if not different:
      if '-v' in args:
        print("++++++++++++++ The Equal Output +++++++++++++")
        print("\n".join(output_lines))
        print("+++++++++++++++++++++++++++++++++++++++++++++")
      print("  " + test + " - passed!")
      tests_passed.append(test)
      tcprint("##teamcity[testFinished name='%s']" % test)
    else:
      if '-v' in args:
        print("============== output from failed " + test + " :")
        print(output)
        print("============== expected output:")
        print(expected_output)
        print("===============================")
      tests_failed.append(test)
      tcprint("##teamcity[testFailed name='%s' message='Output different from expected file']" % test)
      tcprint("##teamcity[testFinished name='%s']" % test)

  print("%i tests passed, %i tests failed." % (
      len(tests_passed), len(tests_failed)))

  if len(tests_failed):
    print("Failed tests:")
    for t in tests_failed:
      print("  " + t)
  print("Ran " + PPSSPP_EXE)


def main():
  global teamcity_mode
  init()
  tests = []
  args = []
  for arg in sys.argv[1:]:
    if arg == '--teamcity':
      teamcity_mode = True
    elif arg[0] == '-':
      args.append(arg)
    else:
      tests.append(arg)

  if not tests:
    if '-g' in args:
      tests = tests_good
    else:
      tests = tests_next + tests_good

  run_tests(tests, args)

main()
