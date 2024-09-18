#!/usr/bin/env python
# Automated script to run the pspautotests test suite in PPSSPP.

import sys
import os
import subprocess
import threading
import glob


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
  # CI
  "ppsspp/PPSSPPHeadless",
  "ppsspp\\PPSSPPHeadless.exe",
]

PPSSPP_EXE = None
TEST_ROOT = "pspautotests/tests/"
TIMEOUT = 5

class Command(object):
  def __init__(self, cmd, data = None):
    self.cmd = cmd
    self.data = data
    self.process = None
    self.output = None
    self.timeout = False

  def run(self, timeout):
    def target():
      self.process = subprocess.Popen(self.cmd, stdin=subprocess.PIPE, stdout=sys.stdout, stderr=subprocess.STDOUT)
      self.process.stdin.write(self.data.encode('utf-8'))
      self.process.stdin.close()
      self.process.communicate()

    thread = threading.Thread(target=target)
    thread.start()

    thread.join(timeout)
    if thread.is_alive():
      self.timeout = True
      if sys.version_info < (2, 6):
        os.kill(self.process.pid, signal.SIGKILL)
      else:
        self.process.terminate()
      thread.join()

    return self.process.returncode

# Test names are the C files without the .c extension.
# These have worked and should keep working always - regression tests.
tests_good = [
  "cpu/cpu_alu/cpu_alu",
  "cpu/cpu_alu/cpu_branch",
  "cpu/cpu_alu/cpu_branch2",
  "cpu/vfpu/colors",
  "cpu/vfpu/convert",
  "cpu/vfpu/gum",
  "cpu/vfpu/matrix",
  "cpu/vfpu/vavg",
  "cpu/icache/icache",
  "cpu/lsu/lsu",
  "cpu/fpu/fpu",

  "audio/atrac/addstreamdata",
  "audio/atrac/atractest",
  "audio/atrac/decode",
  "audio/atrac/getremainframe",
  "audio/atrac/getsoundsample",
  "audio/atrac/ids",
  "audio/atrac/resetpos",
  "audio/atrac/resetting",
  "audio/atrac/second/getinfo",
  "audio/atrac/second/needed",
  "audio/atrac/second/setbuffer",
  "audio/atrac/setdata",
  "audio/mp3/checkneeded",
  "audio/mp3/getbitrate",
  "audio/mp3/getchannel",
  "audio/mp3/getframenum",
  "audio/mp3/getloopnum",
  "audio/mp3/getmaxoutput",
  "audio/mp3/getmpegversion",
  "audio/mp3/getsamplerate",
  "audio/mp3/getsumdecoded",
  "audio/mp3/initresource",
  "audio/mp3/mp3test",
  "audio/mp3/release",
  "audio/mp3/reserve",
  "audio/mp3/setloopnum",
  "audio/output2/changelength",
  "audio/output2/reserve",
  "audio/output2/threads",
  "audio/reverb/basic",
  "audio/reverb/volume",
  "audio/sascore/sascore",
  "audio/sascore/adsrcurve",
  "audio/sascore/getheight",
  "audio/sascore/keyoff",
  "audio/sascore/keyon",
  "audio/sascore/noise",
  "audio/sascore/outputmode",
  "audio/sascore/pause",
  "audio/sascore/pcm",
  "audio/sascore/pitch",
  "audio/sascore/vag",
  "ctrl/ctrl",
  "ctrl/idle/idle",
  "ctrl/sampling/sampling",
  "ctrl/sampling2/sampling2",
  "ctrl/vblank",
  "display/display",
  "display/vblankmulti",
  "display/isstate",
  "display/setframebuf",
  "display/setmode",
  "dmac/dmactest",
  "font/altcharcode",
  "font/charimagerect",
  "font/find",
  "font/fontinfo",
  "font/fontinfobyindex",
  "font/fontlist",
  "font/optimum",
  "font/resolution",
  "font/shadowimagerect",
  "gpu/bounding/count",
  "gpu/bounding/planes",
  "gpu/bounding/vertexaddr",
  "gpu/bounding/viewport",
  "gpu/callbacks/ge_callbacks",
  "gpu/clipping/homogeneous",
  "gpu/clut/address",
  "gpu/clut/masks",
  "gpu/clut/offset",
  "gpu/clut/shifts",
  "gpu/commands/basic",
  "gpu/commands/blend",
  "gpu/commands/blend565",
  "gpu/commands/blocktransfer",
  "gpu/commands/cull",
  "gpu/commands/fog",
  "gpu/commands/material",
  "gpu/complex/complex",
  "gpu/displaylist/alignment",
  "gpu/dither/dither",
  "gpu/filtering/mipmaplinear",
  "gpu/ge/break",
  "gpu/ge/context",
  "gpu/ge/edram",
  "gpu/ge/enqueueparam",
  "gpu/ge/queue",
  "gpu/primitives/indices",
  "gpu/primitives/invalidprim",
  "gpu/primitives/points",
  "gpu/primitives/rectangles",
  "gpu/primitives/trianglefan",
  "gpu/primitives/trianglestrip",
  "gpu/primitives/triangles",
  "gpu/rendertarget/copy",
  "gpu/rendertarget/depal",
  "gpu/signals/pause",
  "gpu/signals/pause2",
  "gpu/signals/suspend",
  "gpu/signals/sync",
  "gpu/texcolors/dxt1",
  "gpu/texcolors/dxt3",
  "gpu/texcolors/dxt5",
  "gpu/texcolors/rgb565",
  "gpu/texcolors/rgba4444",
  "gpu/texcolors/rgba5551",
  "gpu/texfunc/add",
  "gpu/texfunc/blend",
  "gpu/texfunc/decal",
  "gpu/texfunc/modulate",
  "gpu/texfunc/replace",
  "gpu/textures/mipmap",
  "gpu/textures/rotate",
  "gpu/transfer/invalid",
  "gpu/transfer/mirrors",
  "gpu/transfer/overlap",
  "gpu/vertices/colors",
  "gpu/vertices/morph",
  # "gpu/vertices/texcoords",  #  See issue #19093
  "hash/hash",
  "hle/check_not_used_uids",
  "intr/intr",
  "intr/enablesub",
  "intr/suspended",
  "intr/vblank/vblank",
  "io/cwd/cwd",
  "io/open/badparent",
  "jpeg/create",
  "jpeg/delete",
  "jpeg/finish",
  "jpeg/init",
  "loader/bss/bss",
  "malloc/malloc",
  "misc/dcache",
  "misc/deadbeef",
  "misc/libc",
  "misc/sdkver",
  "misc/testgp",
  "misc/timeconv",
  "mstick/mstick",
  "power/cpu",
  "power/power",
  "power/volatile/lock",
  "power/volatile/trylock",
  "power/volatile/unlock",
  "rtc/rtc",
  "rtc/arithmetic",
  "rtc/lookup",
  "string/string",
  "sysmem/freesize",
  "sysmem/memblock",
  "sysmem/sysmem",
  "sysmem/volatile",
  "threads/alarm/alarm",
  "threads/alarm/cancel/cancel",
  "threads/alarm/refer/refer",
  "threads/alarm/set/set",
  "threads/callbacks/callbacks",
  "threads/callbacks/check",
  "threads/callbacks/create",
  "threads/callbacks/delete",
  "threads/callbacks/exit",
  "threads/callbacks/refer",
  "threads/events/events",
  "threads/events/cancel/cancel",
  "threads/events/clear/clear",
  "threads/events/create/create",
  "threads/events/delete/delete",
  "threads/events/poll/poll",
  "threads/events/refer/refer",
  "threads/events/set/set",
  "threads/events/wait/wait",
  "threads/fpl/fpl",
  "threads/fpl/allocate",
  "threads/fpl/cancel",
  "threads/fpl/create",
  "threads/fpl/delete",
  "threads/fpl/free",
  "threads/fpl/priority",
  "threads/fpl/refer",
  "threads/fpl/tryallocate",
  "threads/k0/k0",
  "threads/lwmutex/create",
  "threads/lwmutex/delete",
  "threads/lwmutex/lock",
  "threads/lwmutex/priority",
  "threads/lwmutex/refer",
  "threads/lwmutex/try",
  "threads/lwmutex/try600",
  "threads/lwmutex/unlock",
  "threads/mbx/mbx",
  "threads/mbx/cancel/cancel",
  "threads/mbx/create/create",
  "threads/mbx/delete/delete",
  "threads/mbx/poll/poll",
  "threads/mbx/priority/priority",
  "threads/mbx/receive/receive",
  "threads/mbx/refer/refer",
  "threads/mbx/send/send",
  "threads/msgpipe/msgpipe",
  "threads/msgpipe/cancel",
  "threads/msgpipe/create",
  "threads/msgpipe/data",
  "threads/msgpipe/delete",
  "threads/msgpipe/receive",
  "threads/msgpipe/refer",
  "threads/msgpipe/send",
  "threads/msgpipe/tryreceive",
  "threads/msgpipe/trysend",
  "threads/mutex/cancel",
  "threads/mutex/create",
  "threads/mutex/delete",
  "threads/mutex/lock",
  "threads/mutex/mutex",
  "threads/mutex/priority",
  "threads/mutex/refer",
  "threads/mutex/try",
  "threads/mutex/unlock",
  "threads/mutex/unlock2",
  "threads/semaphores/semaphores",
  "threads/semaphores/cancel",
  "threads/semaphores/create",
  "threads/semaphores/delete",
  "threads/semaphores/fifo",
  "threads/semaphores/poll",
  "threads/semaphores/priority",
  "threads/semaphores/refer",
  "threads/semaphores/signal",
  "threads/semaphores/wait",
  "threads/threads/change",
  "threads/threads/exitstatus",
  "threads/threads/extend",
  "threads/threads/refer",
  "threads/threads/release",
  "threads/threads/rotate",
  "threads/threads/stackfree",
  "threads/threads/start",
  "threads/threads/suspend",
  "threads/threads/threadend",
  "threads/threads/threadmanidlist",
  "threads/threads/threadmanidtype",
  "threads/threads/threads",
  "threads/tls/create",
  "threads/tls/delete",
  "threads/tls/free",
  "threads/tls/priority",
  "threads/tls/refer",
  "threads/vpl/allocate",
  "threads/vpl/cancel",
  "threads/vpl/delete",
  "threads/vpl/fifo",
  "threads/vpl/free",
  "threads/vpl/order",
  "threads/vpl/priority",
  "threads/vpl/refer",
  "threads/vpl/try",
  "threads/vpl/vpl",
  "threads/vtimers/vtimer",
  "threads/vtimers/cancelhandler",
  "threads/vtimers/create",
  "threads/vtimers/delete",
  "threads/vtimers/getbase",
  "threads/vtimers/gettime",
  "threads/vtimers/interrupt",
  "threads/vtimers/refer",
  "threads/vtimers/sethandler",
  "threads/vtimers/settime",
  "threads/vtimers/start",
  "threads/vtimers/stop",
  "threads/wakeup/wakeup",
  "utility/msgdialog/abort",
  "utility/savedata/autosave",
  "utility/savedata/filelist",
  "utility/savedata/makedata",
  "umd/callbacks/umd",
  "umd/register",
  "video/mpeg/ringbuffer/avail",
  "video/mpeg/ringbuffer/construct",
  "video/mpeg/ringbuffer/destruct",
  "video/mpeg/ringbuffer/memsize",
  "video/mpeg/ringbuffer/packnum",
  "video/psmfplayer/break",
  "video/psmfplayer/create",
  "video/psmfplayer/delete",
  "video/psmfplayer/getaudiodata",
  "video/psmfplayer/getaudiooutsize",
  "video/psmfplayer/getcurrentpts",
  "video/psmfplayer/getcurrentstatus",
  "video/psmfplayer/getcurrentstream",
  "video/psmfplayer/getpsmfinfo",
  "video/psmfplayer/releasepsmf",
  "video/psmfplayer/selectspecific",
  "video/psmfplayer/setpsmf",
  "video/psmfplayer/settempbuf",
  "video/psmfplayer/stop",
]

tests_next = [
# These are the next tests up for fixing. These run by default.
  "cpu/fpu/fcr",
  "cpu/vfpu/prefixes",
  "cpu/vfpu/vector",
  "cpu/vfpu/vregs",
  "audio/atrac/replay",
  "audio/atrac/second/resetting",
  "audio/sceaudio/datalen",
  "audio/sceaudio/output",
  "audio/sceaudio/reserve",
  "audio/sascore/setadsr",
  "audio/mp3/infotoadd",
  "audio/mp3/init",
  "audio/mp3/notifyadd",
  "audio/output2/frequency",
  "audio/output2/release",
  "audio/output2/rest",
  "ccc/convertstring",
  "display/hcount",
  "font/fonttest",
  "font/charglyphimage",
  "font/charglyphimageclip",
  "font/charinfo",
  "font/newlib",
  "font/open",
  "font/openfile",
  "font/openmem",
  "font/shadowglyphimage",
  "font/shadowglyphimageclip",
  "font/shadowinfo",
  "gpu/clipping/guardband",
  "gpu/commands/light",
  "gpu/depth/precision",
  "gpu/displaylist/state",
  "gpu/filtering/linear",
  "gpu/filtering/nearest",
  "gpu/filtering/precisionlinear2d",
  "gpu/filtering/precisionlinear3d",
  "gpu/filtering/precisionnearest2d",
  "gpu/filtering/precisionnearest3d",
  "gpu/ge/edramswizzle",
  "gpu/ge/get",
  "gpu/primitives/bezier",
  "gpu/primitives/continue",
  "gpu/primitives/immediate",
  "gpu/primitives/lines",
  "gpu/primitives/linestrip",
  "gpu/primitives/spline",
  "gpu/reflection/reflection",
  "gpu/rendertarget/rendertarget",
  "gpu/signals/continue",
  "gpu/signals/jumps",
  "gpu/signals/simple",
  "gpu/simple/simple",
  "gpu/texmtx/normals",
  "gpu/texmtx/prims",
  "gpu/texmtx/source",
  "gpu/texmtx/uvs",
  "gpu/textures/size",
  "gpu/triangle/triangle",
  "intr/registersub",
  "intr/releasesub",
  "intr/waits",
  "io/directory/directory",
  "io/file/file",
  "io/file/rename",
  "io/io/io",
  "io/iodrv/iodrv",
  "io/open/tty0",
  "jpeg/csc",
  "jpeg/decode",
  "jpeg/decodes",
  "jpeg/decodeycbcr",
  "jpeg/decodeycbcrs",
  "jpeg/getoutputinfo",
  "jpeg/mjpegcsc",
  # Doesn't work on a PSP for security reasons, hangs in PPSSPP currently.
  # Commented out to make tests run much faster.
  #"modules/loadexec/loader",
  "net/http/http",
  "net/primary/ether",
  "power/freq",
  "rtc/convert",
  "sysmem/partition",
  "threads/callbacks/cancel",
  "threads/callbacks/count",
  "threads/callbacks/notify",
  "threads/scheduling/dispatch",
  "threads/scheduling/scheduling",
  "threads/threads/create",
  "threads/threads/terminate",
  "threads/tls/get",
  "threads/vpl/create",
  "umd/io/umd_io",
  "umd/raw_access/raw_access",
  "umd/wait/wait",
  "utility/msgdialog/dialog",
  "utility/savedata/getsize",
  "utility/savedata/idlist",
  # These tests appear to be broken and just hang.
  #"utility/savedata/deletebroken",
  #"utility/savedata/deletedata",
  #"utility/savedata/deleteemptyfilename",
  #"utility/savedata/loadbroken",
  #"utility/savedata/loaddata",
  #"utility/savedata/loademptyfilename",
  #"utility/savedata/saveemptyfilename",
  "utility/savedata/secureversion",
  "utility/savedata/sizes",
  "utility/systemparam/systemparam",
  "video/mpeg/basic",
  "video/pmf/pmf",
  "video/pmf_simple/pmf_simple",
  "video/psmfplayer/basic",
  "video/psmfplayer/configplayer",
  "video/psmfplayer/getvideodata",
  "video/psmfplayer/playmode",
  "video/psmfplayer/selectstream",
  "video/psmfplayer/setpsmfoffset",
  "video/psmfplayer/start",
  "video/psmfplayer/update",
]


# These are the tests we ignore (not important, or impossible to run)
tests_ignored = [
  "kirk/kirk",
  "me/me",
]



def init():
  global PPSSPP_EXE, TEST_ROOT
  if not os.path.exists("pspautotests"):
    if os.path.exists(os.path.dirname(__file__) + "/pspautotests"):
      TEST_ROOT = os.path.dirname(__file__) + "/pspautotests/tests/";
    else:
      print("Please run git submodule init; git submodule update;")
      sys.exit(1)

  if not os.path.exists(TEST_ROOT + "cpu/cpu_alu/cpu_alu.prx"):
    print("Please install the pspsdk and run make in common/ and in all the tests")
    print("(checked for existence of cpu/cpu_alu/cpu_alu.prx)")
    sys.exit(1)

  possible_exes = [glob.glob(f) for f in PPSSPP_EXECUTABLES]
  possible_exes = [x for sublist in possible_exes for x in sublist]
  existing = filter(os.path.exists, possible_exes)
  if existing:
    PPSSPP_EXE = max((os.path.getmtime(f), f) for f in existing)[1]
  else:
    PPSSPP_EXE = None

  if not PPSSPP_EXE:
    print("PPSSPPHeadless executable missing, please build one.")
    sys.exit(1)

def run_tests(test_list, args):
  global PPSSPP_EXE, TIMEOUT
  returncode = 0

  test_filenames = []
  for test in test_list:
    # Try prx first
    elf_filename = TEST_ROOT + test + ".prx"
    if not os.path.exists(elf_filename):
      print("WARNING: no prx, trying elf")
      elf_filename = TEST_ROOT + test + ".elf"

    test_filenames.append(elf_filename)

  if len(test_filenames):
    # TODO: Maybe --compare should detect --graphics?
    cmdline = [PPSSPP_EXE, '--root', TEST_ROOT + '../', '--compare', '--timeout=' + str(TIMEOUT), '@-']
    cmdline.extend([i for i in args if i not in ['-g', '-m', '-b']])

    c = Command(cmdline, '\n'.join(test_filenames))
    returncode = c.run(TIMEOUT * len(test_filenames))

    print("Ran " + ' '.join(cmdline))

  return returncode

def main():
  init()
  tests = []
  args = []
  teamcity = False
  for arg in sys.argv[1:]:
    if arg == '--teamcity':
      args.append(arg)
      teamcity = True
    elif arg[0] == '-':
      args.append(arg)
    else:
      tests.append(arg)

  if not tests:
    if '-g' in args:
      tests = tests_good
    elif '-b' in args:
      tests = tests_next
    else:
      tests = tests_next + tests_good
  elif '-m' in args and '-g' in args:
    tests = [i for i in tests_good if i.startswith(tests[0])]
  elif '-m' in args and '-b' in args:
    tests = [i for i in tests_next if i.startswith(tests[0])]
  elif '-m' in args:
    tests = [i for i in tests_next + tests_good if i.startswith(tests[0])]

  returncode = run_tests(tests, args)
  if teamcity:
    return 0
  return returncode

exit(main())
