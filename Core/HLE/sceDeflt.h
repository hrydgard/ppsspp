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

// NOTE: This is now unmaintained legacy code. sceDeflt is a small leaf library that games ship
// on the disc themselves and that appears in no firmware dump, so we let the game's own module run
// and this HLE is only a fallback for the rare game that imports it without shipping it, and for
// old savestates - whose syscall opcodes index these tables, which is why nothing here is removed.
// See AlwaysDisableHLEFlags in Core/HLE/HLE.cpp.

#pragma once

void Register_sceDeflt();
