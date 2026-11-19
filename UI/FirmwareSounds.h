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
#include <string>
#include <string_view>
#include <vector>

#include "Common/File/Path.h"

// The XMB's sound effects, from an installed firmware dump, for our own UI sounds.
//
// They're in flash0:/vsh/resource/system_plugin.rco. An RCO's tree (with the sound table) is usually
// compressed, but its sound data isn't, and each sound in it is a complete VAG file (PSP ADPCM, mono)
// with its name in the header, so we find them by their "VAGp" magic and don't need the tree.
// In 6.61 there are, all 44.1 kHz:
//   SE2_Cursor.aif        the XMB's tick, 41 ms
//   SE3_Normal_OK.aif     the same as SE2_Cursor
//   SE4_Cansel.aif        the same click, quieter
//   SE5_Category_OK.aif   the same as SE2_Cursor
//   SE8_Option.aif        the same as SE2_Cursor
//   SE09_Error.aif        silent
//   SE12_System_OK.aif    a chime, 0.3 s
//   SE13_System_NG.aif    an error chime, 0.8 s
// The names are cut to the header's 16 bytes, so match them by prefix.
//
// Other RCOs keep their sounds the same way, some stereo: two VAGs with the same name, left then
// right (camera_plugin.rco's shutters, for one).
namespace FirmwareSounds {

struct Sound {
	std::string name;
	int rateInHz = 0;
	int channels = 1;
	std::vector<int16_t> samples;  // Interleaved when stereo.
};

// Whether the installed firmware has a system_plugin.rco to take them from.
bool Available();

// All the sounds in the installed firmware's system_plugin.rco. Empty if there's no firmware.
std::vector<Sound> LoadXMBSounds();

// The first sound whose name starts with prefix, or nullptr.
const Sound *Find(const std::vector<Sound> &sounds, std::string_view prefix);

}  // namespace FirmwareSounds
