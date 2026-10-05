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

"""Check that two PPSSPPHeadless binaries render every GE frame dump in a directory the same.

Frame dumps replay deterministically, so they show a rendering change exactly, where a game run from a savestate
can land on another frame. Each dump is replayed with the baseline binary once and with the new one --runs times;
a dump whose new screenshots differ from each other is reported as nondeterministic (usually a threading race).

Example:
    python3 Tools/dump_compare.py ~/PSP/dumps old/PPSSPPHeadless build-release/PPSSPPHeadless --runs 2
"""

import argparse
import hashlib
import os
import subprocess
import tempfile
from concurrent.futures import ThreadPoolExecutor


def screenshot(binary, dump, out, graphics, timeout):
    if os.path.exists(out):
        os.remove(out)
    try:
        subprocess.run([binary, '--graphics=' + graphics, '--screenshot-save=' + out, dump], capture_output=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return 'timeout'
    if not os.path.exists(out):
        return 'none'
    return hashlib.md5(open(out, 'rb').read()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('dumps', help='a directory of .ppdmp (or zipped) frame dumps, searched recursively')
    parser.add_argument('baseline', help='the PPSSPPHeadless to compare against')
    parser.add_argument('new', help='the PPSSPPHeadless to check')
    parser.add_argument('--runs', type=int, default=1, help='replays with the new binary per dump (default 1)')
    parser.add_argument('--only', action='append', default=[], help='only dumps whose path contains this (repeatable)')
    parser.add_argument('--graphics', default='software', help='backend (default software)')
    parser.add_argument('--jobs', type=int, default=4, help='dumps replayed at once (default 4)')
    parser.add_argument('--timeout', type=int, default=120, help='seconds per replay (default 120)')
    parser.add_argument('--keep', help='keep the screenshots of differing dumps in this directory')
    args = parser.parse_args()
    for b in (args.baseline, args.new):
        if not os.access(b, os.X_OK):
            parser.error('not an executable: ' + b)

    root = os.path.expanduser(args.dumps)
    dumps = []
    for d, _, files in os.walk(root):
        for f in files:
            if f.lower().endswith(('.ppdmp', '.zip')):
                dumps.append(os.path.join(d, f))
    dumps = sorted(d for d in dumps if not args.only or any(o in d for o in args.only))
    work = tempfile.mkdtemp(prefix='dump_compare_')

    def one(dump):
        key = hashlib.md5(dump.encode()).hexdigest()[:12]
        base = screenshot(args.baseline, dump, os.path.join(work, key + '_base.png'), args.graphics, args.timeout)
        news = [screenshot(args.new, dump, os.path.join(work, '%s_new%d.png' % (key, i)), args.graphics, args.timeout) for i in range(args.runs)]
        if all(n == base for n in news):
            status = 'same'
        elif len(set(news)) == 1:
            status = 'DIFF'
        else:
            status = 'NONDET'
        return status, dump, key, base, news

    with ThreadPoolExecutor(args.jobs) as ex:
        results = list(ex.map(one, dumps))
    differing = [r for r in results if r[0] != 'same']
    for status, dump, key, base, news in differing:
        print('%-6s %s  base %s  new %s' % (status, os.path.relpath(dump, root), base[:8], ' '.join(n[:8] for n in news)))
        if args.keep:
            os.makedirs(args.keep, exist_ok=True)
            for f in os.listdir(work):
                if f.startswith(key):
                    os.replace(os.path.join(work, f), os.path.join(args.keep, f.replace(key, os.path.basename(dump))))
    print('%d dumps, %d differ' % (len(results), len(differing)))


if __name__ == '__main__':
    main()
