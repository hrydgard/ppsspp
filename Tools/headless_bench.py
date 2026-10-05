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

"""Compare the speed of PPSSPPHeadless binaries on games, savestates or frame dumps.

Each game runs for a fixed emulated time with every binary, the binaries alternating from round to round so
that slow drift (heat, background work) hits them evenly. Reported per binary, as medians:

- wall time, which is what a player sees, but swings by several percent between identical runs;
- CPU time (user + system, all threads), which says how much work was done in total;
- instructions retired and cycles, where the OS can count them (macOS through /usr/bin/time -l, Linux
  through perf stat). Instructions vary by well under 0.1% between identical runs, so they show small
  changes that wall time hides. Cycles also count stalls (division, memory), which instructions miss.

Runs on battery power are throttled unpredictably, so the script checks for mains power first.

Example:
    python3 Tools/headless_bench.py --rounds 4 --emu-secs 5 --memstick ~/.config/ppsspp \\
        --game "~/PSP/LocoRoco.cso@~/.config/ppsspp/PSP/PPSSPP_STATE/UCES00304_1.01_0.ppst" \\
        old/PPSSPPHeadless build-release/PPSSPPHeadless

A game is ISO[@SAVESTATE], or a frame dump (.ppdmp), which replays and exits on its own.
"""

import argparse
import glob
import json
import os
import platform
import re
import shutil
import statistics
import subprocess
import sys
import time


def on_mains_power():
    """True on mains power, False on battery, None if it can't tell (or there's no battery)."""
    system = platform.system()
    if system == 'Darwin':
        try:
            out = subprocess.run(['pmset', '-g', 'batt'], capture_output=True, text=True).stdout
        except OSError:
            return None
        if "'AC Power'" in out:
            return True
        if "'Battery Power'" in out:
            return False
        return None
    if system == 'Linux':
        mains = None
        for supply in glob.glob('/sys/class/power_supply/*'):
            try:
                kind = open(os.path.join(supply, 'type')).read().strip()
                if kind == 'Mains':
                    online = open(os.path.join(supply, 'online')).read().strip() == '1'
                    mains = online or bool(mains)
            except OSError:
                pass
        return mains
    if system == 'Windows':
        import ctypes

        class SystemPowerStatus(ctypes.Structure):
            _fields_ = [('ACLineStatus', ctypes.c_ubyte), ('BatteryFlag', ctypes.c_ubyte),
                        ('BatteryLifePercent', ctypes.c_ubyte), ('SystemStatusFlag', ctypes.c_ubyte),
                        ('BatteryLifeTime', ctypes.c_ulong), ('BatteryFullLifeTime', ctypes.c_ulong)]
        status = SystemPowerStatus()
        if not ctypes.windll.kernel32.GetSystemPowerStatus(ctypes.byref(status)):
            return None
        return {0: False, 1: True}.get(status.ACLineStatus)
    return None


def low_power_mode():
    if platform.system() != 'Darwin':
        return False
    try:
        out = subprocess.run(['pmset', '-g'], capture_output=True, text=True).stdout
    except OSError:
        return False
    return re.search(r'lowpowermode\s+1', out) is not None


def check_power(allow_battery):
    while True:
        mains = on_mains_power()
        if mains is not False and not low_power_mode():
            if mains is None:
                print('Note: could not tell whether this machine runs on mains power.')
            return
        problem = 'Low power mode is on' if mains else 'This machine is running on battery'
        print('%s: CPU speed will vary and the results won\'t be comparable.' % problem)
        if allow_battery:
            print('Continuing anyway (--allow-battery).')
            return
        if not sys.stdin.isatty():
            sys.exit('Connect mains power (or pass --allow-battery) and run again.')
        answer = input('Connect mains power and press Enter to check again, or type "go" to continue anyway: ')
        if answer.strip().lower() == 'go':
            return


def counter_prefix():
    """How to wrap a run to count its instructions and cycles, and which kind it is."""
    system = platform.system()
    if system == 'Darwin' and os.path.exists('/usr/bin/time'):
        return ['/usr/bin/time', '-l'], 'darwin'
    if system == 'Linux' and shutil.which('perf'):
        return ['perf', 'stat', '-x', ',', '-e', 'instructions,cycles'], 'perf'
    return [], None


def parse_counters(kind, stderr):
    counts = {}
    if kind == 'darwin':
        for name, key in (('instructions retired', 'inst'), ('cycles elapsed', 'cycles')):
            m = re.search(r'(\d+)\s+' + name, stderr)
            if m:
                counts[key] = int(m.group(1))
    elif kind == 'perf':
        for line in stderr.splitlines():
            fields = line.split(',')
            if len(fields) > 2 and fields[0].isdigit():
                if fields[2].startswith('instructions'):
                    counts['inst'] = int(fields[0])
                elif fields[2].startswith('cycles'):
                    counts['cycles'] = int(fields[0])
    return counts


def run_once(binary, game, args, prefix, kind):
    iso, _, state = game.partition('@')
    iso = os.path.expanduser(iso)
    cmd = [binary, '--graphics=' + args.graphics]
    if args.memstick:
        cmd.append('--memstick=' + os.path.expanduser(args.memstick))
    is_dump = iso.lower().endswith('.ppdmp')
    if not is_dump:
        cmd += ['--timeout-emulated=%g' % args.emu_secs, '--timeout-wall=%d' % args.wall_limit]
    if state:
        cmd.append('--state=' + os.path.expanduser(state))
    cmd += args.extra
    cmd.append(iso)

    start = time.perf_counter()
    proc = subprocess.Popen(prefix + cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
                            errors='replace')
    if hasattr(os, 'wait4'):
        # The child's own CPU time, wherever this script runs other things.
        _, status, usage = os.wait4(proc.pid, 0)
        proc.returncode = os.waitstatus_to_exitcode(status)
        stdout, stderr = proc.stdout.read(), proc.stderr.read()
        cpu = usage.ru_utime + usage.ru_stime
    else:
        stdout, stderr = proc.communicate()
        cpu = None
    wall = time.perf_counter() - start

    # Assert that it got where it should, so a run that never started can't pass for a fast one.
    if proc.returncode != 0:
        raise RuntimeError('%s exited with %d on %s:\n%s' % (binary, proc.returncode, game, stderr[-2000:]))
    output = stdout + stderr
    if not is_dump and 'TIMEOUT (emulated)' not in output:
        raise RuntimeError('%s didn\'t run %s for its emulated time:\n%s' % (binary, game, output[-2000:]))
    # Loaded State, or a warning that starts with Loaded.
    if state and not re.search(r'^Loaded', output, re.M):
        raise RuntimeError('%s didn\'t load the savestate for %s:\n%s' % (binary, game, (stdout + stderr)[-2000:]))
    result = {'wall': wall}
    if cpu is not None:
        result['cpu'] = cpu
    result.update(parse_counters(kind, stderr))
    return result


def report(game, binaries, results):
    print('\n' + game)
    keys = [k for k in ('wall', 'cpu', 'inst', 'cycles') if all(results[b] and k in results[b][0] for b in binaries)]
    base = {}
    for b in binaries:
        cols = []
        for k in keys:
            values = [r[k] for r in results[b]]
            med = statistics.median(values)
            spread = 100.0 * (max(values) - min(values)) / med if med else 0.0
            if b == binaries[0]:
                base[k] = med
                delta = ''
            else:
                delta = ' (%+.2f%%)' % (100.0 * (med / base[k] - 1.0))
            value = ('%.2fs' % med) if k in ('wall', 'cpu') else ('%.4g' % med)
            cols.append('%s %s%s ±%.1f%%' % (k, value, delta, spread / 2))
        print('  %-28s %s' % (os.path.basename(os.path.dirname(b)) + '/' + os.path.basename(b), '  '.join(cols)))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('binaries', nargs='+', help='PPSSPPHeadless binaries; the first is the baseline')
    parser.add_argument('--game', action='append', default=[], help='ISO[@SAVESTATE] or a .ppdmp (repeatable)')
    parser.add_argument('--games', help='JSON file: a list of games as for --game')
    parser.add_argument('--rounds', type=int, default=4, help='runs per binary and game (default 4)')
    parser.add_argument('--warmup', type=int, default=1, help='uncounted rounds first (default 1)')
    parser.add_argument('--emu-secs', type=float, default=5, help='emulated seconds per run (default 5)')
    parser.add_argument('--wall-limit', type=int, default=300, help='wall-clock limit per run (default 300)')
    parser.add_argument('--graphics', default='software', help='backend (default software)')
    parser.add_argument('--memstick', help='memory stick directory, for firmware modules (see docs/debugging.md)')
    parser.add_argument('--allow-battery', action='store_true', help='run even on battery power')
    parser.add_argument('extra', nargs=argparse.REMAINDER, help='after --: more PPSSPPHeadless options')
    args = parser.parse_args()
    if args.extra and args.extra[0] == '--':
        args.extra = args.extra[1:]

    games = list(args.game)
    if args.games:
        games += json.load(open(args.games))
    if not games:
        parser.error('no games: pass --game or --games')
    binaries = [os.path.abspath(b) for b in args.binaries]
    for b in binaries:
        if not os.access(b, os.X_OK):
            parser.error('not an executable: ' + b)

    check_power(args.allow_battery)
    if hasattr(os, 'getloadavg') and os.getloadavg()[0] > 1.5:
        print('Note: the load average is %.1f, other work running will skew the results.' % os.getloadavg()[0])
    prefix, kind = counter_prefix()
    if not kind:
        print('Note: no instruction and cycle counts here (macOS /usr/bin/time or Linux perf needed).')

    for game in games:
        results = {b: [] for b in binaries}
        for r in range(args.warmup + args.rounds):
            # Alternate the order, so neither binary always runs right after the other.
            order = binaries if r % 2 == 0 else binaries[::-1]
            for b in order:
                result = run_once(b, game, args, prefix, kind)
                if r >= args.warmup:
                    results[b].append(result)
        report(game, binaries, results)


if __name__ == '__main__':
    main()
