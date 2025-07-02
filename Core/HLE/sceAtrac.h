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

#include "Core/HLE/sceAudiocodec.h"
#include "Core/Util/AtracTrack.h"
#include "Core/HLE/AtracCtx.h"

class PointerWrap;

void Register_sceAtrac3plus();
void __AtracInit();
void __AtracDoState(PointerWrap &p);
void __AtracShutdown();
int __AtracMaxContexts();

void __AtracNotifyLoadModule(int version, u32 crc, u32 bssAddr, int bssSize);
void __AtracNotifyUnloadModule();

constexpr int PSP_MAX_ATRAC_IDS = 6;

class AtracBase;

// For debugger use ONLY.
const AtracBase *__AtracGetCtx(int i, u32 *type);

// External interface used by sceSas, see ATRAC_STATUS_FOR_SCESAS.
u32 AtracSasAddStreamData(int atracID, u32 bufPtr, u32 bytesToAdd);
void AtracSasDecodeData(int atracID, u8* outbuf, int *SamplesNum, int *finish);
int AtracSasBindContextAndGetID(u32 contextAddr);

// To provide checkboxes in the debugger UI.
// This setting is not saved.
bool *__AtracMuteFlag(int atracID);
