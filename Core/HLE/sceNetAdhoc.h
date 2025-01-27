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

#ifdef _MSC_VER
#pragma pack(push,1)
#endif
typedef struct SceNetAdhocDiscoverParam {
	u32_le unknown1; // SleepMode? (ie. 0 on on Legend Of The Dragon, 1 on Dissidia 012)
	char   groupName[ADHOCCTL_GROUPNAME_LEN];
	u32_le unknown2; // size of something? (ie. 0x3c on Legend Of The Dragon, 0x14 on Dissidia 012) // Note: the param size is 0x14 may be it can contains extra data too?
	u32_le result; // inited to 0?
} PACK SceNetAdhocDiscoverParam;

typedef struct ProductStruct { // Similar to SceNetAdhocctlAdhocId ?
	s32_le unknown; // Unknown, set to 0 // Product Type ?
	char product[PRODUCT_CODE_LENGTH]; // Game ID (Example: ULUS10000)
} PACK ProductStruct;

#ifdef _MSC_VER
#pragma pack(pop)
#endif

struct AdhocctlRequest {
	u8 opcode;
	SceNetAdhocctlGroupName group;
};

struct AdhocSendTarget {
	u32 ip;
	u16 port; // original port
	u16 portOffset; // port offset specific for this target IP
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

enum AdhocDiscoverStatus : int
{
	NET_ADHOC_DISCOVER_STATUS_NONE = 0,
	NET_ADHOC_DISCOVER_STATUS_IN_PROGRESS = 1,
	NET_ADHOC_DISCOVER_STATUS_COMPLETED = 2,
};

enum AdhocDiscoverResult : int
{
	NET_ADHOC_DISCOVER_RESULT_NO_PEER_FOUND = 0, // Initial value
	NET_ADHOC_DISCOVER_RESULT_CANCELED = 1, // CANCELED or STOPPED?
	NET_ADHOC_DISCOVER_RESULT_PEER_FOUND = 2,
	NET_ADHOC_DISCOVER_RESULT_ABORTED = 3, // Internal Error occured?
};


class PointerWrap;

void Register_sceNetAdhoc();
void Register_sceNetAdhocDiscover();
void Register_sceNetAdhocctl();


u32_le __CreateHLELoop(u32_le* loopAddr, const char* sceFuncName, const char* hleFuncName, const char* tagName = NULL);
void __NetAdhocInit();
void __NetAdhocShutdown();
void __NetAdhocDoState(PointerWrap &p);
void __UpdateAdhocctlHandlers(u32 flags, u32 error);

bool __NetAdhocConnected();

// Called from netdialog (and from sceNetApctl)
// NOTE: use hleCall for sceNet* ones!
int sceNetAdhocctlGetState(u32 ptrToStatus);
int sceNetAdhocctlCreate(const char * groupName);
int sceNetAdhocctlConnect(const char* groupName);
int sceNetAdhocctlJoin(u32 scanInfoAddr);
int sceNetAdhocctlScan();
int sceNetAdhocctlGetScanInfo(u32 sizeAddr, u32 bufAddr);
int sceNetAdhocctlDisconnect();
int sceNetAdhocctlInit(int stackSize, int prio, u32 productAddr);
int sceNetAdhocctlTerm();

int NetAdhocctl_Term();
int NetAdhocctl_GetState();
int NetAdhocctl_Create(const char* groupName);
int NetAdhoc_Term();

// May need to use these from sceNet.cpp
extern bool netAdhocInited;
extern bool netAdhocctlInited;
extern bool g_adhocServerConnected;
extern bool netAdhocGameModeEntered;
extern s32 netAdhocDiscoverStatus;

extern int netAdhocEnterGameModeTimeout;
extern int adhocDefaultTimeout; //3000000 usec
extern int adhocDefaultDelay; //10000
extern int adhocExtraDelay; //20000
extern int adhocEventPollDelay; //100000; // Seems to be the same with PSP_ADHOCCTL_RECV_TIMEOUT
extern int adhocMatchingEventDelay; //30000
extern int adhocEventDelay; //1000000
extern std::recursive_mutex adhocEvtMtx;
extern int IsAdhocctlInCB;

extern u32 dummyThreadHackAddr;
extern u32_le dummyThreadCode[3];
extern u32 matchingThreadHackAddr;
extern u32_le matchingThreadCode[3];

// Exposing those for the matching routines
// NOTE: use hleCall for sceNet* ones!
int sceNetAdhocPdpSend(int id, const char* mac, u32 port, void* data, int len, int timeout, int flag);
int sceNetAdhocPdpRecv(int id, void* addr, void* port, void* buf, void* dataLength, u32 timeout, int flag);
int sceNetAdhocPdpCreate(const char* mac, int port, int bufferSize, u32 flag);

int NetAdhoc_SetSocketAlert(int id, s32_le flag);
int NetAdhocPdp_Delete(int id, int unknown);
