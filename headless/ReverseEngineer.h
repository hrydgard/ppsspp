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

#include "Common/CommonTypes.h"

struct ReverseEngineerOptions {
	// PRX/ELF to load. A host path; a PSP path like "flash0:/kd/libmp3.prx", resolved against the
	// configured NAND directory; or "disc0:/PSP_GAME/USRDIR/MODULES/LIBDEFLT.PRX", read out of the
	// disc image named as the positional argument. The last is how you look at the copy of a
	// library a game ships rather than the firmware's.
	std::string modulePath;
	// The disc image a "disc0:" modulePath is read from.
	std::string discPath;
	// Where to write the report. Created if missing.
	std::string outDir;
	// If set, only this function is disassembled (by name, or "0x08801234").
	std::string funcFilter;
	// Optional .syms file applied before dumping, so names show up in every caller.
	std::string symsFile;
	// If non-zero, modulePath is a flat code image rather than a PRX: it's copied to this
	// address and scanned directly, with no loader, no relocation and no imports/exports.
	u32 rawBase = 0;
	bool verbose = false;
};

// Loads a module standalone (no game), analyzes it, and writes a report. Returns a process
// exit code.
int RunReverseEngineer(const ReverseEngineerOptions &opts);

// Decrypts one encrypted PSP file and writes the plaintext, without loading anything.
// Handles any container pspDecryptPRX() knows a tag for - including the ME images in
// flash0:/kd/resource, which are ordinary tagged containers with the signature blanked, so the
// normal module loader won't touch them. Returns a process exit code.
int RunDecryptFile(const std::string &inPath, const std::string &outPath);
