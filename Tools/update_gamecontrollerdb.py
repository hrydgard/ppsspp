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

"""Re-sync assets/gamecontrollerdb.txt with the community SDL_GameControllerDB.

The file is upstream's database followed by PPSSPP's own mappings, which start at
PPSSPP_MARKER. This replaces the upstream part with the current upstream file and
keeps the PPSSPP mappings, except those whose GUID (on the same platform) upstream
now has. SDL lets a later mapping replace an earlier one, so a stale PPSSPP line
would otherwise override upstream's.

Usage: python Tools/update_gamecontrollerdb.py [--upstream FILE]
"""

import argparse
import os
import sys
import urllib.request

UPSTREAM_URL = "https://raw.githubusercontent.com/mdqinc/SDL_GameControllerDB/master/gamecontrollerdb.txt"
PPSSPP_MARKER = "# The below is from PPSSPP's old gamecontrollerdb.txt."

DB_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "assets", "gamecontrollerdb.txt")

# Section comments in the PPSSPP part, for lines without a platform: field.
SECTION_PLATFORMS = {
	"# Windows": "Windows",
	"# OS X": "Mac OS X",
	"# Mac OS X": "Mac OS X",
	"# Linux": "Linux",
}


def normalize_guid(guid, platform):
	"""Converts pre-2.0.5 GUIDs the way SDL_PrivateGetGamepadGUIDFromMappingString() does."""
	guid = guid.lower()
	if len(guid) != 32:
		return guid
	if platform == "Windows" and guid[20:] == "504944564944":
		return "03000000" + guid[0:4] + "0000" + guid[4:8] + "000000000000"
	if platform == "Mac OS X" and guid[4:16] == "000000000000" and guid[20:] == "000000000000":
		return "03000000" + guid[0:4] + "0000" + guid[16:20] + "000000000000"
	return guid


def parse_mapping(line, section_platform):
	"""Returns (guid, platform) for a mapping line, or None for comments and blanks."""
	line = line.strip()
	if not line or line.startswith("#"):
		return None
	fields = line.split(",")
	platform = section_platform
	for field in fields[2:]:
		if field.startswith("platform:"):
			platform = field[len("platform:"):]
	return (normalize_guid(fields[0], platform), platform)


def section_platform_of(line, current):
	for prefix, platform in SECTION_PLATFORMS.items():
		if line.startswith(prefix):
			return platform
	return current


def main():
	parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
	parser.add_argument("--upstream", help="Use this local copy of upstream's gamecontrollerdb.txt instead of downloading it")
	args = parser.parse_args()

	with open(DB_PATH, "r", encoding="utf-8", newline="") as f:
		current = f.read()
	newline = "\r\n" if "\r\n" in current else "\n"
	current_lines = current.splitlines()

	if PPSSPP_MARKER not in current_lines:
		print("Marker line not found in %s, giving up" % DB_PATH, file=sys.stderr)
		return 1
	ppsspp_lines = current_lines[current_lines.index(PPSSPP_MARKER):]

	if args.upstream:
		with open(args.upstream, "r", encoding="utf-8") as f:
			upstream = f.read()
	else:
		with urllib.request.urlopen(UPSTREAM_URL) as response:
			upstream = response.read().decode("utf-8")
	upstream_lines = upstream.splitlines()
	while upstream_lines and not upstream_lines[-1].strip():
		upstream_lines.pop()

	upstream_keys = set()
	platform = None
	for line in upstream_lines:
		platform = section_platform_of(line, platform)
		key = parse_mapping(line, platform)
		if key:
			upstream_keys.add(key)

	kept = []
	dropped = []
	platform = None
	for line in ppsspp_lines:
		platform = section_platform_of(line, platform)
		key = parse_mapping(line, platform)
		if key and key in upstream_keys:
			dropped.append(line)
		else:
			kept.append(line)

	with open(DB_PATH, "w", encoding="utf-8", newline="") as f:
		f.write(newline.join(upstream_lines + [""] + kept) + newline)

	print("Upstream: %d mappings. PPSSPP: kept %d lines, dropped %d now covered upstream:" % (len(upstream_keys), len(kept), len(dropped)))
	for line in dropped:
		print("  " + line.split(",")[0] + " " + line.split(",")[1])
	return 0


if __name__ == "__main__":
	sys.exit(main())
