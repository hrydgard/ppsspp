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

#include "Core/HLE/proAdhoc.h"

#ifdef _MSC_VER
#pragma pack(push,1)
#endif
typedef struct MatchingArgs {
	u32_le data[6]; // ContextID, EventID, bufAddr[ to MAC], OptLen, OptAddr[, EntryPoint]
} PACK MatchingArgs;
#ifdef _MSC_VER
#pragma pack(pop)
#endif

class PointerWrap;

int NetAdhocMatching_Term();

void DoNetAdhocMatchingInited(PointerWrap &p);
void DoNetAdhocMatchingThreads(PointerWrap &p);
void ZeroNetAdhocMatchingThreads();
void SaveNetAdhocMatchingInited();
void RestoreNetAdhocMatchingInited();

void Register_sceNetAdhocMatching();
void __NetAdhocMatchingInit();
void __NetAdhocMatchingShutdown();

extern bool netAdhocMatchingInited;
extern int adhocMatchingEventDelay; //30000
