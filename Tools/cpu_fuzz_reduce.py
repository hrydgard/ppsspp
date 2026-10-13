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

"""Runs the CPUEquivalence fuzzer (see docs/cpu-equivalence.md). If a case crashes, finds it and
reduces its code to the instructions still needed for the crash.

usage: cpu_fuzz_reduce.py UNITTEST_EXE FIRSTSEED COUNT [CLASSES]
"""

import os
import re
import subprocess
import sys
import tempfile

CODE_LINE = re.compile(r'    [0-9a-f]{8}  ')


def run(exe, seed, count, classes, code=None, verbose=False):
    env = dict(os.environ, CI='1', PPSSPP_CPUFUZZ_SEED=str(seed), PPSSPP_CPUFUZZ_COUNT=str(count))
    if classes:
        env['PPSSPP_CPUFUZZ_CLASSES'] = classes
    if code:
        env['PPSSPP_CPUFUZZ_CODE'] = ','.join(code)
    if verbose:
        env['PPSSPP_CPUFUZZ_VERBOSE'] = '1'
    # A file, not a pipe: a crash would lose whatever is still buffered in a pipe.
    with tempfile.TemporaryFile('w+') as log:
        try:
            rc = subprocess.run([exe, 'CPUEquivalence'], env=env, stdout=log, stderr=subprocess.STDOUT, timeout=1800).returncode
        except subprocess.TimeoutExpired:
            rc = -1
        log.seek(0)
        return rc, log.read()


def crashed(rc):
    # 0 passed, 1 failed (differences in strict mode), 2 reported by the harness as failing.
    return rc not in (0, 1, 2)


def main():
    if len(sys.argv) < 4:
        print('usage: cpu_fuzz_reduce.py UNITTEST_EXE FIRSTSEED COUNT [CLASSES]')
        return 1
    exe, firstSeed, count = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
    classes = sys.argv[4] if len(sys.argv) > 4 else ''

    rc, out = run(exe, firstSeed, count, classes, verbose=True)
    if not crashed(rc):
        print(''.join(l for l in out.splitlines(True) if 'cases differ' in l), end='')
        return 0

    lines = out.splitlines()
    idx = max(i for i, l in enumerate(lines) if re.match(r'.* seed \d+$', l))
    seed = int(lines[idx].split()[-1])
    core = lines[idx].rsplit(' seed', 1)[0]
    code = [l.split()[0] for l in lines[idx + 1:] if CODE_LINE.match(l)]
    messages = [l for l in lines[idx + 1:] if not CODE_LINE.match(l)]
    print('Crash (rc %d) in %s, seed %d' % (rc, core, seed))
    print('\n'.join(messages[:3]))

    if not crashed(run(exe, seed, 1, classes, code)[0]):
        print('Does not crash on its own, so it depends on the cases before it.')
        return 1
    i = 0
    while i < len(code):
        trial = code[:i] + code[i + 1:]
        if trial and crashed(run(exe, seed, 1, classes, trial)[0]):
            code = trial
        else:
            i += 1
    print('Reduced: PPSSPP_CPUFUZZ_SEED=%d PPSSPP_CPUFUZZ_CODE=%s' % (seed, ','.join(code)))
    _, out = run(exe, seed, 1, classes, code, verbose=True)
    shown = set()
    for l in out.splitlines():
        if (CODE_LINE.match(l) or l.startswith('(')) and l not in shown:
            shown.add(l)
            print(l)
    return 0


if __name__ == '__main__':
    sys.exit(main())
