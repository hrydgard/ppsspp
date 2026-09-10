// Copyright (c) 2012- PPSSPP Project.

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

#include <algorithm>
#include <cstring>
#include <set>

#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Core/FileSystems/FileSystem.h"

void PSPFileInfo::DoState(PointerWrap &p) {
	auto s = p.Section("PSPFileInfo", 1);
	if (!s)
		return;

	Do(p, name);
	Do(p, size);
	Do(p, access);
	Do(p, exists);
	Do(p, type);
	Do(p, atime);
	Do(p, ctime);
	Do(p, mtime);
	Do(p, isOnSectorSystem);
	Do(p, startSector);
	Do(p, numSectors);
	Do(p, sectorSize);
}


// FAT 8.3 short names.
//
// The PSP's memory stick is FAT, so every file there has both its long name and a generated 8.3
// short name. sceIoDread returns the short name in the first bytes of d_private, ahead of the long
// name, and some games read that instead of d_name and then open files by it - Crazy Taxi: Fare
// Wars does, for its custom soundtracks, and silently finds no music at all without it.
//
// We generate the names ourselves and resolve them ourselves, rather than leaning on the host:
//  * Linux, macOS and Android have no concept of 8.3 short names at all.
//  * Windows does keep its own aliases, but generates them differently - it only counts up to ~4
//    and then switches to a hash (sample-15s-cbr-128kbps.mp3 becomes SA6615~1.MP3) - and 8.3 name
//    creation can be disabled per volume, so it can't be relied on even there.
//
// TODO: The names a real PSP generates are unverified. There's no hardware test covering d_private
// (pspautotests io/directory checks d_name, size and attr only), so this implements the ordinary
// FAT rule - cleaned-up base, truncated, plus a ~N counter for collisions - which is what typical
// FAT drivers do. If a hardware test later shows the PSP numbering or truncation differs, this
// function is the only place that needs to change.
//
// One deliberate difference from real FAT: there the short name is written to disk when the file is
// created, so it's stable for the life of the file. We derive it from the directory listing, so
// adding or removing a file can renumber the ~N suffixes of the others. That only matters if a game
// holds a short name across a change to the directory, which nothing is expected to do.

static bool IsValidShortNameChar(char c) {
	if (c >= 'A' && c <= 'Z')
		return true;
	if (c >= '0' && c <= '9')
		return true;
	// The remaining characters FAT permits unescaped in a short name.
	return strchr("$%'-_@~`!(){}^#&", c) != nullptr;
}

// Uppercases and strips anything FAT wouldn't accept, reporting whether that lost information -
// which is what decides between using the name as-is and appending a ~N counter.
static std::string CleanShortNamePart(std::string_view part, size_t maxLen, bool *lossy) {
	std::string out;
	for (char c : part) {
		if (c >= 'a' && c <= 'z') {
			// Short names are case insensitive, so this on its own isn't a loss.
			c = c - 'a' + 'A';
		}
		if (c == ' ' || c == '.') {
			// Dropped entirely rather than escaped, matching FAT.
			*lossy = true;
			continue;
		}
		if (!IsValidShortNameChar(c)) {
			c = '_';
			*lossy = true;
		}
		if (out.size() >= maxLen) {
			*lossy = true;
			break;
		}
		out.push_back(c);
	}
	return out;
}

// Whether the name's capitalisation survives without a long-name entry. FAT keeps one flag for
// the base and one for the extension, but the PSP only honours the base one - so a lowercase
// extension forces a long name entry, and with it a ~1 suffix, while a lowercase base alone
// doesn't. That's why hardware gives "shrt" -> SHRT but "readme.txt" -> README~1.TXT.
static bool ShortNameCaseSurvives(std::string_view base, std::string_view ext) {
	bool lower = false, upper = false;
	for (char c : base) {
		if (c >= 'a' && c <= 'z') {
			lower = true;
		} else if (c >= 'A' && c <= 'Z') {
			upper = true;
		}
	}
	if (lower && upper) {
		return false;
	}
	for (char c : ext) {
		if (c >= 'a' && c <= 'z') {
			return false;
		}
	}
	return true;
}

void GenerateFatShortNames(const std::vector<PSPFileInfo> &listing, std::vector<std::string> *shortNames) {
	shortNames->clear();
	shortNames->reserve(listing.size());

	std::set<std::string> taken;
	for (const PSPFileInfo &info : listing) {
		// "." and ".." are their own short names and don't take part in numbering.
		if (info.name == "." || info.name == "..") {
			shortNames->push_back(info.name);
			continue;
		}

		// Split off the extension at the last dot. A leading dot is part of the name, not an
		// extension separator, so ".hidden" has no extension.
		size_t dot = info.name.find_last_of('.');
		bool lossy = false;
		std::string_view baseIn = info.name;
		std::string_view extIn;
		if (dot != std::string::npos && dot != 0) {
			baseIn = std::string_view(info.name).substr(0, dot);
			extIn = std::string_view(info.name).substr(dot + 1);
		} else if (dot == 0) {
			lossy = true;
		}

		if (!ShortNameCaseSurvives(baseIn, extIn)) {
			lossy = true;
		}

		std::string base = CleanShortNamePart(baseIn, 8, &lossy);
		std::string ext = CleanShortNamePart(extIn, 3, &lossy);
		if (base.empty()) {
			base = "_";
			lossy = true;
		}

		std::string candidate;
		if (!lossy) {
			candidate = ext.empty() ? base : base + "." + ext;
			// A name that needs no mangling can still collide, since short names ignore case.
			if (taken.find(candidate) != taken.end()) {
				candidate.clear();
			}
		}

		for (int n = 1; candidate.empty(); n++) {
			char suffix[16];
			snprintf(suffix, sizeof(suffix), "~%d", n);
			// The counter has to fit inside the eight characters along with the stem.
			std::string stem = base.substr(0, std::max((size_t)1, 8 - strlen(suffix)));
			std::string attempt = stem + suffix;
			if (!ext.empty()) {
				attempt += "." + ext;
			}
			if (taken.find(attempt) == taken.end()) {
				candidate = attempt;
			}
		}

		taken.insert(candidate);
		shortNames->push_back(candidate);
	}
}
