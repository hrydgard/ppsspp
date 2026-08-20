// Copyright (c) 2026- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#pragma once

#include <string>
#include <vector>

#include "Common/CommonTypes.h"

class Path;

// Unpacks the firmware image inside an official PSP updater (PSP/GAME/UPDATE/EBOOT.PBP).
//
// The updater's DATA.PSAR is a flat sequence of encrypted, compressed file records. The real
// updater installs them by executing on the PSP, but the archive can be walked directly - each
// record carries its own name, sizes and a standard PRX-style encryption header, so all it needs
// is the KIRK engine and the PRX decrypter we already have.
//
// The main use is pulling flash0:/font out of an updater the user supplies, which is why a
// prefix filter is part of the API rather than something the caller filters afterwards.

enum class PSARCompression {
	None,
	Zlib,
	KL4E,
	KL3E,
	LZR,
	Unknown,
};

const char *PSARCompressionToString(PSARCompression c);

// PSP hardware revisions, as the updater numbers them. An updater carries one file list per
// model, so which one you resolve names against decides both what a file is called and whether
// it's part of that model's firmware at all.
enum class PSPModelGeneration {
	Any = 0,   // Whichever list names a file first. Use this to extract everything.
	PSP_1000 = 1,
	PSP_2000 = 2,
	PSP_3000 = 3,
	PSP_4000 = 4,
	PSP_N1000 = 5,   // PSP Go
	PSP_6000 = 6,
	PSP_7000 = 7,
	PSP_9000 = 9,
	PSP_11000 = 11,
	MAX = 12,
};

const char *PSPModelGenerationToString(PSPModelGeneration generation);
// Accepts "01g".."12g", a bare number, or "any". Returns false if it's none of those.
bool PSPModelGenerationFromString(std::string_view name, PSPModelGeneration *generation);

struct PSARUnpackOptions {
	// Only unpack entries whose name starts with this, e.g. "flash0:/font/". Case insensitive.
	// Empty means everything. Entries whose real name we can't recover are never matched by a
	// non-empty filter.
	std::string prefixFilter;
	// Which model's file list to resolve names against. Anything that model's list doesn't name
	// isn't part of its firmware, and is skipped.
	PSPModelGeneration model = PSPModelGeneration::Any;
	// Walk and report, but don't write any files.
	bool listOnly = false;
	// Log a line per entry. Off by default - an updater holds well over a thousand of them.
	bool verbose = false;
};

struct PSARUnpackStats {
	std::string firmwareVersion;
	int entries = 0;
	int directories = 0;
	int written = 0;
	int skippedByFilter = 0;
	int nameTables = 0;          // Entries that were file lists rather than files.
	int unnamed = 0;             // Short names no file list claimed.
	int otherModel = 0;          // Files that belong to a model other than the requested one.
	int failed = 0;
	// How many entries used each compression, indexed by PSARCompression.
	int compressionCounts[6]{};
};

// psar points at the DATA.PSAR contents, starting with the "PSAR" magic.
bool UnpackPSAR(const u8 *psar, size_t psarSize, const Path &outputDir, const PSARUnpackOptions &options, PSARUnpackStats *stats, std::string *error);

// Convenience wrapper: opens an updater EBOOT.PBP, finds its DATA.PSAR and unpacks that.
bool UnpackUpdaterPBP(const Path &pbpFilename, const Path &outputDir, const PSARUnpackOptions &options, PSARUnpackStats *stats, std::string *error);
