// Copyright (c) 2013- PPSSPP Project.

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
	u32_le data[6]; //ContextID, Opcode, bufAddr[ to MAC], OptLen, OptAddr[, EntryPoint]
} PACK MatchingArgs;
#ifdef _MSC_VER
#pragma pack(pop)
#endif

class PointerWrap;

void Register_sceNetAdhoc();

u32_le __CreateHLELoop(u32_le* loopAddr, const char* sceFuncName, const char* hleFuncName, const char* tagName = NULL);
void __NetAdhocInit();
void __NetAdhocShutdown();
void __NetAdhocDoState(PointerWrap &p);
void __UpdateAdhocctlHandlers(u32 flags, u32 error);
void __UpdateMatchingHandler(MatchingArgs params);

// I have to call this from netdialog
int sceNetAdhocctlCreate(const char * groupName);

// May need to use these from sceNet.cpp
extern bool netAdhocInited;
extern bool netAdhocctlInited;
extern bool networkInited;
extern int adhocDefaultTimeout;
extern int adhocEventPollDelayMS;
extern int adhocMatchingEventDelayMS;
extern int adhocEventDelayMS; // This will affect the duration of "Connecting..." dialog/message box in .Hack//Link and Naruto Ultimate Ninja Heroes 3
extern std::recursive_mutex adhocEvtMtx;
extern int IsAdhocctlInCB;

int NetAdhocMatching_Term();
int NetAdhocctl_Term();
int NetAdhoc_Term();
