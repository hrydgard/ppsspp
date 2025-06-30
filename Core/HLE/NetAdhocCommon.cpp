// Copyright (c) 2025- PPSSPP Project.

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

#include "Core/HLE/NetAdhocCommon.h"

#include "sceKernelMemory.h"
#include "Core/MemMapHelpers.h"

u32 dummyThreadHackAddr = 0;
u32_le dummyThreadCode[3];


void netAdhocValidateLoopMemory() {
    // Allocate Memory if it wasn't valid/allocated after loaded from old SaveState
    if (!dummyThreadHackAddr || strcmp("dummythreadhack", kernelMemory.GetBlockTag(dummyThreadHackAddr)) != 0) {
        u32 blockSize = sizeof(dummyThreadCode);
        dummyThreadHackAddr = kernelMemory.Alloc(blockSize, false, "dummythreadhack");
        if (dummyThreadHackAddr)
            Memory::Memcpy(dummyThreadHackAddr, dummyThreadCode, sizeof(dummyThreadCode));
    }
}

int netAdhocEnterGameModeTimeout = 15000000; // 15 sec as default timeout, to wait for all players to join
int adhocDefaultTimeout = 5000000; //2000000 usec // For some unknown reason, sometimes it tooks more than 2 seconds for Adhocctl Init to connect to AdhocServer on localhost (normally only 10 ms), and sometimes it tooks more than 1 seconds for built-in AdhocServer to be ready (normally only 1 ms)

int adhocDefaultDelay = 10000; //10000
int adhocExtraDelay = 20000; //20000
int adhocEventPollDelay = 100000; //100000; // Same timings with PSP_ADHOCCTL_RECV_TIMEOUT ?
int adhocEventDelay = 2000000; //2000000 on real PSP ?
std::recursive_mutex adhocEvtMtx;

int IsAdhocctlInCB = 0;


u32 matchingThreadHackAddr = 0;
u32_le matchingThreadCode[3];
