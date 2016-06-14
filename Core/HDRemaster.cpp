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

// TODO: Do all of the remasters aside from Monster Hunter/Shin Sangoku use double texture coordinates?
extern const struct HDRemaster g_HDRemasters[] = {
	{ "NPJB40001", 0x04000000, false }, // MONSTER HUNTER PORTABLE 3rd HD Ver.
	{ "NPJB40002", 0x04000000, true }, // K-ON Houkago Live HD Ver
	{ "NPJB40003", 0x04000000, false }, // Shin Sangoku Musou Multi Raid 2 HD Ver
	{ "ULJM05170", 0x04000000, true, "ULJM-05170|55C069C631B22685|0001|G" }, // Eiyuu Densetsu Sora no Kiseki FC Kai HD Edition
	{ "ULJM05277", 0x04C00000, true, "ULJM-05277|0E8D71AFAA4F62D8|0001|G" }, // Eiyuu Densetsu: Sora no Kiseki SC Kai HD Edition
	{ "ULJM05353", 0x04C00000, true, "ULJM-05353|0061DA67EBD6B9C6|0001|G" }, // Eiyuu Densetsu: Sora no Kiseki 3rd Kai HD Edition
	{ "ULUS12345", 0x04000000, false, "ULUS-12345|6E197A8FB304B3DB|0001|G" }, // Saints Row 2 / Undercover - alpha (needs extra ram, not a remaster)
	{ "UCUS12345", 0x04000000, false, "UCUS-12345|909D53E8B45B4B4A|0001|G" }, // The Elder Scrolls Travels Oblivion USA Beta - 09062006 (needs extra ram, not a remaster)
	{ "ABCD01234561", 0x04000000, false, "UCUS-00000|BC7AE2442B7CD085|0001|G" }, // The Elder Scrolls Travels Oblivion USA Beta - 21112006 (needs extra ram, not a remaster)
	{ "ABCD01234561", 0x04000000, false, "UCUS-00000|6035DE628DC1C924|0001|G" }, // The Elder Scrolls Travels Oblivion USA Beta - 31012007 (needs extra ram, not a remaster)
	{ "ABCD01234561", 0x04000000, false, "UCUS-00000|BBE65F2837A488C2|0001|G" }, // The Elder Scrolls Travels Oblivion USA Beta - 01022007 (needs extra ram, not a remaster)
	{ "ABCD01234561", 0x04000000, false, "UCUS-00000|55BE9ADC13B14D65|0001|G" }, // The Elder Scrolls Travels Oblivion USA Beta - 27042007 (needs extra ram, not a remaster)
	{ "PETR00010", 0x04000000, false, "PETR-00010|0C274C92FF78330E|0001|G" }, // Melodie - alpha (needs extra ram, not a remaster)
};

const size_t g_HDRemastersCount = ARRAY_SIZE(g_HDRemasters);
