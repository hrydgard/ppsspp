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

#include <functional>
#include <string>
#include <vector>

#include "Common/CommonTypes.h"

class FileLoader;
class Path;

// Reads .PKG files - the NPDRM container Sony distributed downloadable content in. We're only
// interested in one flavor: PSP *game updates*, which hold a patched EBOOT (PBOOT.PBP) plus the
// data files the patch replaces. Installing one puts them in PSP/GAME/<DISC_ID>/, and the game
// then boots from there with the original disc still supplying everything the patch doesn't
// override - see FindGameUpdatePBOOT() in Core/PSPLoaders.cpp.
//
// The whole package past the header is AES-128-CTR, and every PBOOT seen so far is encrypted with
// a PRX tag we already have a key for, so nothing here needs new crypto secrets.
//
// docs/pkg_notes.md describes the format and what these packages turned out to contain.

struct PkgItem {
	std::string name;
	u64 dataOffset = 0;   // Relative to the encrypted area, not to the file.
	u64 dataSize = 0;
	u8 pspType = 0;       // 0x90 selects the PSP key, anything else the PS3 key.
	u8 flags = 0;         // Content type. See docs/pkg_notes.md for the values.

	bool IsDirectory() const { return flags == 4 || flags == 18; }
};

struct PkgInfo {
	std::string contentId;      // "JP0177-ULJM05681_00-PJD2UPDATEVR0101"
	std::string titleId;        // "ULJM05681"
	u32 contentType = 0;        // 7 for PSP. See kPkgContentTypePSP.
	std::string title;          // From the package's own PARAM.SFO.
	std::string category;       // "PP" for a game update.

	// A game update carries a PBOOT.PBP whose own PARAM.SFO says what it patches. Without one
	// there's nothing here we know how to install.
	bool isGameUpdate = false;
	std::string discId;         // The disc this patches, e.g. "ULJM05681".
	std::string discVersion;    // The disc version it patches, e.g. "1.00".
	std::string appVer;         // The patch's own version, e.g. "01.01".
	std::string systemVer;      // Firmware the patch asks for, e.g. "6.20".
	std::string pbootTitle;     // "Update 2.01", when the patch bothers to name itself.

	std::vector<PkgItem> items;
};

const u32 kPkgContentTypePSP = 7;

class PkgReader {
public:
	// Parses the header, the item table and the two PARAM.SFOs. Doesn't take ownership of the
	// loader, which has to outlive the reader.
	bool Open(FileLoader *loader, std::string *error);

	const PkgInfo &Info() const { return info_; }

	// Decrypts `size` bytes at `offset` within an item.
	bool ReadItemData(const PkgItem &item, u64 offset, size_t size, u8 *out);
	// The whole item, for small ones. Fails rather than allocating more than maxSize.
	bool ReadItem(const PkgItem &item, std::vector<u8> *out, size_t maxSize = 4 * 1024 * 1024);

private:
	bool ReadEncrypted(u64 offset, size_t size, const u8 *key, u8 *out);
	const u8 *ItemKey(const PkgItem &item) const;
	bool ReadPBOOTInfo(const PkgItem &pboot);

	FileLoader *loader_ = nullptr;
	u64 dataOffset_ = 0;
	u64 dataSize_ = 0;
	u8 riv_[16]{};
	u8 mainKey_[16]{};
	PkgInfo info_;
};

// Where an item ends up inside the installed game folder, or empty for the ones that shouldn't be
// installed at all. A package wraps its payload in USRDIR/CONTENT/, PS3-style, and also carries
// store metadata (PARAM.SFO, PS3LOGO.DAT, the icons) that isn't part of the PSP-side install.
std::string PkgItemInstallPath(const PkgItem &item);

// What the install will take up on disk. Package contents aren't compressed, so this is exact
// rather than an estimate - modulo the filesystem's own per-file overhead.
u64 PkgInstalledSize(const PkgInfo &info);

// Unpacks the installable items into destDir, which should be the game folder itself
// (PSP/GAME/<DISC_ID>). progress is called with 0..1 as it goes, and may be null.
bool InstallPkg(PkgReader &reader, const Path &destDir, const std::function<void(float)> &progress, std::string *error);
