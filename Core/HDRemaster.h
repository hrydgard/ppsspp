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

#include <string>
#include "CommonTypes.h"

// This bool is the key to having the HD remasters work.
// We keep it set to false by default in PSPLoaders.cpp
// in order to keep the 99% of other PSP games working happily.
extern bool g_RemasterMode;

extern bool g_DoubleTextureCoordinates;

struct HDRemaster {
	std::string gameID;
	u32 MemorySize;
	bool DoubleTextureCoordinates;
};

// TODO: Use UMD_DATA.bin to differentiate the Eiyuu games from the regular PSP editions.
// TODO: Do all of the remasters aside from Monster Hunter/Shin Sangoku use double texture coordinates?
const struct HDRemaster g_HDRemasters[] = {
	{ "NPJB40001", 0x4000000, false }, // MONSTER HUNTER PORTABLE 3rd HD Ver.
	{ "NPJB40002", 0x4000000, true }, // K-ON Houkago Live HD Ver
	{ "NPJB40003", 0x4000000, false }, // Shin Sangoku Musou Multi Raid 2 HD Ver
	// { "ULJM05170", 0x4000000, true }, // Eiyuu Densetsu Sora no Kiseki FC Kai HD Edition
	// { "ULJM05277", 0x4C00000, true }, // Eiyuu Densetsu: Sora no Kiseki SC Kai HD Edition, game needs 76 MB
	// { "ULJM05353", 0x4C00000, true }, // Eiyuu Densetsu: Sora no Kiseki 3rd Kai HD Edition, game needs 76 MB
};
