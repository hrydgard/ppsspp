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

#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "Common/File/Path.h"

// Optional, runtime-loaded maxcso (https://github.com/unknownbrackets/maxcso) support.
// PPSSPP doesn't ship or link maxcso. If the user drops a maxcso library built with its small
// C API (maxcso_capi.h) into memstick/PSP/SYSTEM, it's picked up at startup and the
// MaxCSO tools menu becomes available.
namespace MaxCSO {

// Matches maxcso_mode in maxcso_capi.h.
enum class Mode {
	Decompress = 0,
	Fast = 1,
	Standard = 2,
	Maximum = 3,
};

// Looks for the library in GetSysDirectory(DIRECTORY_SYSTEM) and loads it. Safe to call more
// than once; only the first call does anything.
void Init();

bool IsAvailable();
// Version reported by the library, like "1.13.0". Empty if not loaded.
std::string Version();
// Where the library was loaded from. Empty if not loaded.
Path LibraryPath();

// The file name we look for on this platform.
const char *LibraryFileName();

// progress(fraction) is called from the calling thread, fraction in [0, 1].
typedef std::function<void(float)> ProgressFunc;

// Blocking. Compresses an ISO to CSO, or decompresses a CSO/ZSO/DAX back to ISO.
// On failure, a partially written output is deleted and *error gets the reason.
bool Process(const Path &input, const Path &output, Mode mode, const ProgressFunc &progress, std::string *error);

}  // namespace MaxCSO
