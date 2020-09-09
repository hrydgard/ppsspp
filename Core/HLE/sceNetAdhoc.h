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

#include <deque>
#include "Core/HLE/proAdhoc.h"

typedef struct MatchingArgs {
	u32 data[6]; //ContextID, Opcode, bufAddr[ to MAC], OptLen, OptAddr[, EntryPoint]
} MatchingArgs;

struct AdhocctlRequest {
	u8 opcode;
	SceNetAdhocctlGroupName group;
};

struct AdhocSendTarget {
	u32 ip;
	u16 port; // original port
};

struct AdhocSendTargets {
	int length;
	std::deque<AdhocSendTarget> peers;
	bool isBroadcast;
};

struct AdhocSocketRequest {
	int type;
	int id; // PDP/PTP socket id
	void* buffer;
	s32_le* length;
	u32 timeout;
	u64 startTime;
	SceNetEtherAddr* remoteMAC;
	u16_le* remotePort;
};

enum AdhocSocketRequestType : int
{
	PTP_CONNECT = 0,
	PTP_ACCEPT = 1,
	PTP_SEND = 2,
	PTP_RECV = 3,
	PTP_FLUSH = 4,
	PDP_SEND = 5,
	PDP_RECV = 6,
	ADHOC_POLL_SOCKET = 7,
};

class PointerWrap;

void Register_sceNetAdhoc();

u32 __CreateHLELoop(u32_le* loopAddr, const char* sceFuncName, const char* hleFuncName, const char* tagName = NULL);
void __NetAdhocInit();
void __NetAdhocShutdown();
void __NetAdhocDoState(PointerWrap &p);
void __UpdateAdhocctlHandlers(u32 flags, u32 error);
void __UpdateMatchingHandler(MatchingArgs params);

// I have to call this from netdialog
int sceNetAdhocctlGetState(u32 ptrToStatus);
int sceNetAdhocctlCreate(const char * groupName);
int sceNetAdhocctlConnect(const char* groupName);
int sceNetAdhocctlJoin(u32 scanInfoAddr);
int sceNetAdhocctlScan();
int sceNetAdhocctlGetScanInfo(u32 sizeAddr, u32 bufAddr);

int NetAdhocMatching_Term();
int NetAdhocctl_Term();
int NetAdhocctl_GetState();
int NetAdhocctl_Create(const char* groupName);
int NetAdhoc_Term();

// May need to use these from sceNet.cpp
extern bool netAdhocInited;
extern bool netAdhocctlInited;
extern bool networkInited;
extern int adhocDefaultTimeout;
extern int adhocExtraPollDelayMS;
extern int adhocEventPollDelayMS;
extern int adhocMatchingEventDelayMS;
extern int adhocEventDelayMS; // This will affect the duration of "Connecting..." dialog/message box in .Hack//Link and Naruto Ultimate Ninja Heroes 3
extern std::recursive_mutex adhocEvtMtx;
extern int IsAdhocctlInCB;

extern u32 dummyThreadHackAddr;
extern u32_le dummyThreadCode[3];
extern u32 matchingThreadHackAddr;
extern u32_le matchingThreadCode[3];

