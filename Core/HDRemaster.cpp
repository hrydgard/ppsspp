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

#include "base/basictypes.h"
#include "Core/HDRemaster.h"

bool g_RemasterMode;
bool g_DoubleTextureCoordinates;

extern const struct HDRemaster g_HDRemasters[] = {
	{ "NPJB40001", "NPJB-40001|9E5068A8E12454A5|0001|G", 0x04000000, false}, // Monster Hunter Portable 3rd
	{ "NPJB40002", "NPJB-40002|91B2BE27FA482C91|0001|G", 0x04000000, true},  // K-ON! Houkago Live!!
	{ "NPJB40003", "NPJB-40003|679CE3A6453B0F68|0001|G", 0x04000000, false}, // Shin Sangoku Musou: Multi Raid 2
	{ "ULJM05170", "ULJM-05170|55C069C631B22685|0001|G", 0x04000000, false}, // Eiyuu Densetsu: Sora no Kiseki FC
	{ "ULJM05277", "ULJM-05277|0E8D71AFAA4F62D8|0001|G", 0x04C00000, false}, // Eiyuu Densetsu: Sora no Kiseki SC
	{ "ULJM05353", "ULJM-05353|0061DA67EBD6B9C6|0001|G", 0x04C00000, false}  // Eiyuu Densetsu: Sora no Kiseki the 3rd
};

const size_t g_HDRemastersCount = ARRAY_SIZE(g_HDRemasters);
