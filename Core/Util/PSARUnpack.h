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

#include <ctime>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "Common/CommonTypes.h"

class Path;
class IFileSystem;

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
	// Called on the unpacking thread after every entry, with how far through the archive we are
	// (0.0 to 1.0). Unpacking a full firmware takes a while, so a UI wants this.
	std::function<void(float)> progress;
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

// Finds the firmware image in whatever an updater arrives as and unpacks it:
//  - a downloaded updater EBOOT.PBP, where the archive is its DATA.PSAR
//  - a disc updater's DATA.BIN, which is the same archive without the PBP wrapper
//  - a game ISO/CSO/CHD, which is searched for PSP_GAME/SYSDIR/UPDATE/DATA.BIN
// The last one is the interesting case: most UMDs carry a firmware updater, so the fonts can come
// from whatever game the user already has rather than a separate download.
bool UnpackUpdater(const Path &filename, const Path &outputDir, const PSARUnpackOptions &options, PSARUnpackStats *stats, std::string *error);

// What a game disc's bundled firmware updater says about itself. All of this comes from the
// PARAM.SFO and the directory entry next to the archive, so gathering it costs a couple of small
// reads - no decryption, and the archive itself is never touched.
struct BundledUpdateInfo {
	bool present = false;
	std::string version;      // "6.61". Can be empty even when present, if the SFO is unreadable.
	std::string title;        // The updater's full SFO title, e.g. "PSP(tm) Update ver 6.61".
	s64 archiveSize = 0;      // Size of DATA.BIN, i.e. how much firmware is in there.
	// When DATA.BIN was written, as Unix UTC seconds. 0 if the disc doesn't record one, which
	// is normal for the shapes that aren't really an ISO.
	s64 mtime = 0;

	// "6.61 (2011-01-25)", or just the version if there's no date. Empty if there's no updater.
	std::string Describe() const;
};

// Reads the above out of a disc that's already open, whether that's an ISOFileSystem the caller
// mounted or the running game's disc0:. pathPrefix is what to stick in front of "PSP_GAME/..." -
// "/" for a freshly mounted image, "disc0:/" for the meta file system.
bool ReadBundledUpdateInfo(IFileSystem *fs, std::string_view pathPrefix, BundledUpdateInfo *info);

// The version string an updater advertises ("6.61"), read from the PARAM.SFO next to it - no
// decryption needed, so it's cheap enough to check every disc with. Empty if there's no updater.
std::string ReadUpdaterVersion(const Path &filename);

// The same three, for the disc mounted as disc0: - i.e. the game that's running. These read
// through the mounted filesystem instead of opening the image a second time, which also means
// they work for the shapes that aren't an image at all, like a folder-based "disc".
//
// This is the path the font extraction is meant to take: while a game is running, ask whether its
// disc carries an updater, and if so unpack just flash0:/font out of it.
bool MountedDiscHasUpdater();
std::string ReadMountedDiscUpdaterVersion();
bool UnpackUpdaterFromMountedDisc(const Path &outputDir, const PSARUnpackOptions &options, PSARUnpackStats *stats, std::string *error);
