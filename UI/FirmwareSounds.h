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
// The camera's sounds are in flash0:/vsh/resource/camera_plugin.rco, the same way but stereo: each
// is two VAGs with the same name, left then right. Camera01.aif is the shutter (two short bursts of
// noise, 0.16 s), the one the camera accessory's radioshack_plugin.rco also has; Camera02/03 are
// longer shutters, Digi01/02 beeps.
namespace FirmwareSounds {

struct Sound {
	std::string name;
	int rateInHz = 0;
	int channels = 1;
	std::vector<int16_t> samples;  // Interleaved when stereo.
};

// Whether there's a system_plugin.rco under nandRoot to take them from.
bool Available(const Path &nandRoot);

// All the sounds in system_plugin.rco under nandRoot. Empty if there's no firmware dump.
std::vector<Sound> LoadXMBSounds(const Path &nandRoot);
// The same for camera_plugin.rco.
std::vector<Sound> LoadCameraSounds(const Path &nandRoot);

// The first sound whose name starts with prefix, or nullptr.
const Sound *Find(const std::vector<Sound> &sounds, std::string_view prefix);

}  // namespace FirmwareSounds
