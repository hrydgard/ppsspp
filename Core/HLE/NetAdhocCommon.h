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

// Old comments regarding the value of adhocDefaultTimeout:
// 3000000 usec
//2000000 usec // For some unknown reason, sometimes it tooks more than 2 seconds for Adhocctl Init to connect to AdhocServer on localhost (normally only 10 ms), and sometimes it tooks more than 1 seconds for built-in AdhocServer to be ready (normally only 1 ms)
constexpr int adhocDefaultTimeout = 5000000;

constexpr int adhocDefaultDelay = 10000; //10000
constexpr int adhocExtraDelay = 20000; //20000
constexpr int adhocEventPollDelay = 100000; //100000; // Seems to be the same with PSP_ADHOCCTL_RECV_TIMEOUT

// Old comments regarding the value of adhocEventDelay:
//1000000
constexpr int adhocEventDelay = 2000000; //2000000 on real PSP ?

extern std::recursive_mutex adhocEvtMtx;

// TODO: this one is broken, perhaps delete it entirely?
extern int IsAdhocctlInCB;


extern u32 matchingThreadHackAddr;
extern u32_le matchingThreadCode[3];

extern bool g_adhocServerConnected;

constexpr u32 defaultLastRecvDelta = 10000; //10000 usec worked well for games published by Falcom (ie. Ys vs Sora Kiseki, Vantage Master Portable)
