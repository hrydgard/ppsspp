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

#include <functional>
#include <string>

#include "Common/File/Path.h"

// Writes PSP disc images as CSO v1 or plain ISO. The input is anything PPSSPP can play as a disc
// (ISO, CSO, CHD, a PSN EBOOT.PBP), read through its own block devices, so a repack is one pass
// with no temporary file. The presets match maxcso's (LunaMoo/maxcso) as the Un/Pack menu used them.
namespace CSOCompress {

enum class Preset {
	Max = 0,            // Zopfli, skipped on blocks libdeflate can't shrink below 98%; 256K blocks.
	Great = 1,          // libdeflate level 12, 64K blocks. Much faster, slightly bigger, quicker to read.
	PSPCompatible = 2,  // zlib level 9, 2K blocks: what every PSP custom firmware reads.
};

// Return false to cancel.
typedef std::function<bool(float progress)> ProgressFunc;

// Blocking, and uses every core. Never overwrites, and deletes a partial output on failure or
// cancel (*cancelled is set then).
bool Compress(const Path &input, const Path &output, Preset preset, const ProgressFunc &progress, std::string *error, bool *cancelled);
bool Extract(const Path &input, const Path &output, const ProgressFunc &progress, std::string *error, bool *cancelled);

}  // namespace CSOCompress
