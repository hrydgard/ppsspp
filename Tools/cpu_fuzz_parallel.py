#!/usr/bin/env python3
# Runs the CPUEquivalence fuzzer in several processes, each on its own range of seeds, and sums
# up the results. A case depends only on its seed, so this finds the same differences as one run.
#
# Usage: cpu_fuzz_parallel.py UNITTEST_EXE [--count N] [--seed S] [-j JOBS]
# The PPSSPP_CPUFUZZ_* environment variables other than COUNT and SEED pass through, so
#   PPSSPP_CPUFUZZ_OPS=dot cpu_fuzz_parallel.py build/PPSSPPUnitTest
# fuzzes the dot products.

import argparse
import os
import re
import subprocess
import sys
from collections import Counter


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("exe")
    parser.add_argument("--count", type=int, default=2000)
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("-j", "--jobs", type=int, default=os.cpu_count())
    args = parser.parse_args()

    jobs = max(1, min(args.jobs, args.count))
    procs = []
    start = args.seed
    for j in range(jobs):
        n = args.count // jobs + (1 if j < args.count % jobs else 0)
        env = dict(os.environ, PPSSPP_CPUFUZZ_SEED=str(start), PPSSPP_CPUFUZZ_COUNT=str(n))
        procs.append((start, subprocess.Popen([args.exe, "CPUEquivalence"], env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)))
        start += n

    differ = Counter()
    culprits = Counter()
    failed = False
    for first, p in procs:
        out, _ = p.communicate()
        if p.returncode != 0:
            failed = True
            print("Seeds from %d: exit code %d" % (first, p.returncode))
        in_culprits = False
        for line in out.splitlines():
            m = re.match(r"(.+): (\d+) of \d+ cases differ", line)
            if m:
                differ[m.group(1)] += int(m.group(2))
                in_culprits = False
                continue
            if line.startswith("Reported differences by last instruction"):
                in_culprits = True
                continue
            m = re.match(r"\s+(\d+)\s+(.+)$", line)
            if in_culprits and m:
                culprits[m.group(2)] += int(m.group(1))
                continue
            # The minimized reports.
            if line.startswith(("Seed ", "    ")):
                print(line)

    if culprits:
        print("Reported differences by last instruction:")
        for name, n in culprits.most_common():
            print("%6d  %s" % (n, name))
    for core, n in differ.items():
        print("%s: %d of %d cases differ from the interpreter" % (core, n, args.count))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
