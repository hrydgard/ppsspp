#!/bin/bash
## Copyright (c) 2012- PPSSPP Project.

## This program is free software: you can redistribute it and/or modify
## it under the terms of the GNU General Public License as published by
## the Free Software Foundation, version 2.0 or later versions.

## This program is distributed in the hope that it will be useful,
## but WITHOUT ANY WARRANTY; without even the implied warranty of
## MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
## GNU General Public License 2.0 for more details.

## A copy of the GPL 2.0 should have been included with the program.
## If not, see http://www.gnu.org/licenses/

## Official git repository and contact information can be found at
## https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

GIT_VERSION_FILE=$(dirname $0)/../git-version.cpp

if [ -e "$GIT_VERSION_FILE" ]; then
	# Skip updating the file if PPSSPP_GIT_VERSION_NO_UPDATE is 1.
	results=$(grep '^#define PPSSPP_GIT_VERSION_NO_UPDATE 1' "$GIT_VERSION_FILE")
	if [ "$results" != "" ]; then
		exit 0
	fi
fi

GIT_VERSION=$(git describe --always)
if [ "$GIT_VERSION" == "" ]; then
	echo "Unable to update git-version.cpp, git not on path." 1>&2

	echo "// This is a generated file." > "$GIT_VERSION_FILE"
	echo >> "$GIT_VERSION_FILE"
	echo 'const char *PPSSPP_GIT_VERSION = "unknown";' >> "$GIT_VERSION_FILE"
	exit 0
fi

# Don't modify the file if it already has the current version.
if [ -e "$GIT_VERSION_FILE" ]; then
	results=$(grep "$GIT_VERSION" "$GIT_VERSION_FILE")
	if [ "$results" != "" ]; then
		exit 0
	fi
fi

echo "// This is a generated file." > "$GIT_VERSION_FILE"
echo >> "$GIT_VERSION_FILE"
echo 'const char *PPSSPP_GIT_VERSION = "'"$GIT_VERSION"'";' >> "$GIT_VERSION_FILE"
echo >> "$GIT_VERSION_FILE"
echo "// If you don't want this file to update/recompile, change to 1." >> "$GIT_VERSION_FILE"
echo "#define PPSSPP_GIT_VERSION_NO_UPDATE 0" >> "$GIT_VERSION_FILE"
