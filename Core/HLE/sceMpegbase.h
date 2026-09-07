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

#include <vector>

#include "Common/CommonTypes.h"

void Register_sceMpegbase();

// Called per boot, from __MpegInit.
void __MpegBaseInit();

// The PES payload sceMpegBasePESpacketCopy gathered for a given destination. On hardware that copy
// lands in Media Engine memory, which sceVideocodec would then read back; we keep it here instead.
// Returns nullptr if nothing was copied to that address.
const std::vector<u8> *MpegBaseGetPESPacket(u32 dest);
