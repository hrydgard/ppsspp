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

#pragma once

#include "CommonTypes.h"
#include "Swap.h"

#include <mutex>

extern u32 dummyThreadHackAddr;
extern u32_le dummyThreadCode[3];

void netAdhocValidateLoopMemory();

extern int netAdhocEnterGameModeTimeout;

// TODO: constify
extern int adhocDefaultTimeout; //3000000 usec
extern int adhocDefaultDelay; //10000
extern int adhocExtraDelay; //20000
extern int adhocEventPollDelay; //100000; // Seems to be the same with PSP_ADHOCCTL_RECV_TIMEOUT
extern int adhocEventDelay; //1000000

extern std::recursive_mutex adhocEvtMtx;

// TODO: this one is broken, perhaps delete it entirely?
extern int IsAdhocctlInCB;


extern u32 matchingThreadHackAddr;
extern u32_le matchingThreadCode[3];

extern bool g_adhocServerConnected;