#!/usr/bin/env python3
# Runs a multi-console ad hoc pspautotest on several PPSSPPHeadless instances at once, the way
# tests/net/adhoc/pair/record.py runs it on several PSPs.
#
# A test NAME is a set of programs NAME_host.prx, NAME_client.prx, NAME_client2.prx, ... with an
# .expected each. The host runs as instance 1 with the built-in ad hoc server, each client as the next
# instance, all at PSP speed (--realtime) since they wait for each other over the network. Instance N
# binds to 127.0.0.N, so on macOS the loopback aliases have to exist first:
#   sudo ifconfig lo0 alias 127.0.0.2 up   (and .3, .4, ... for more clients)
#
# With --relay, the instances use the relay in relay mode (on 27313, next to adhocctl on 27312). The host's
# built-in server runs one, unless an aemu_postoffice server is already running on this machine (see
# ext/aemu_postoffice/hosting.md). The relay tells players apart by MAC, so they can all share 127.0.0.1
# and no aliases are needed.
#
# Usage: python3 Tools/adhoc_multi.py [--headless PATH] [--relay] net/adhoc/pair/basic [more tests...]

import argparse
import os
import subprocess
import sys
import tempfile
import time

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
TESTS_DIR = os.path.join(ROOT, "pspautotests", "tests")
TIMEOUT = 120


def find_headless():
	candidates = []
	for d in os.listdir(ROOT):
		for name in ("PPSSPPHeadless", "PPSSPPHeadless.exe"):
			path = os.path.join(ROOT, d, name)
			if d.startswith("build") and os.path.isfile(path):
				candidates.append(path)
	if not candidates:
		sys.exit("No PPSSPPHeadless found in build*/; pass --headless")
	return max(candidates, key=os.path.getmtime)


def roles_of(test):
	base = os.path.join(TESTS_DIR, test)
	roles = ["host", "client"]
	n = 2
	while os.path.exists("%s_client%d.prx" % (base, n)):
		roles.append("client%d" % n)
		n += 1
	for role in roles:
		if not os.path.exists("%s_%s.prx" % (base, role)):
			sys.exit("Missing %s_%s.prx" % (test, role))
	return roles


RELAY_INI = """[Network]
proAdhocServer = 127.0.0.1
AdhocServerRelayMode = 1
"""


def run_test(headless, test, extra, relay_ini):
	roles = roles_of(test)
	memstick = os.path.join(os.path.dirname(headless), "memstick")
	procs = []
	for i, role in enumerate(roles):
		cmd = [headless, "--root", os.path.join(ROOT, "pspautotests") + "/", "--compare", "--timeout-wall=%d" % TIMEOUT,
			"--graphics=software", "--memstick=" + memstick, "--realtime", "--instance=%d" % (i + 1)]
		if relay_ini:
			cmd.append("--appendconfig=" + relay_ini)
		if i == 0:
			cmd.append("--adhoc-server")
		cmd += extra
		cmd.append(os.path.join(TESTS_DIR, "%s_%s.prx" % (test, role)))
		procs.append((role, subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)))
		if i == 0:
			# Let the host's built-in server come up before the clients log in to it.
			time.sleep(1.0)

	passed = True
	for role, proc in procs:
		out, _ = proc.communicate(timeout=TIMEOUT + 30)
		ok = proc.returncode == 0
		passed = passed and ok
		print("%s %s_%s" % ("PASS" if ok else "FAIL", test, role))
		if not ok:
			sys.stdout.write(out.decode("utf-8", "replace"))
	return passed


def main():
	parser = argparse.ArgumentParser()
	parser.add_argument("--headless", default=None)
	parser.add_argument("--relay", action="store_true", help="Use a local aemu_postoffice server in relay mode")
	parser.add_argument("tests", nargs="+")
	args, extra = parser.parse_known_args()
	headless = args.headless or find_headless()
	relay_ini = None
	if args.relay:
		f = tempfile.NamedTemporaryFile("w", suffix=".ini", delete=False)
		f.write(RELAY_INI)
		f.close()
		relay_ini = f.name
	failed = [t for t in args.tests if not run_test(headless, t, extra, relay_ini)]
	print("%d of %d passed" % (len(args.tests) - len(failed), len(args.tests)))
	return 1 if failed else 0


if __name__ == "__main__":
	sys.exit(main())
