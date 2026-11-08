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
#include <string>
#include "Core/HLE/proAdhoc.h"

#ifdef _MSC_VER
#pragma pack(push,1)
#endif

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
	SceNetEtherAddr mac;
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

typedef struct SceNetAdhocDiscoverParam {
	u32_le mode;  // 0 sleeps in the DISCOVER state until another console is near (Legend of the Dragon), 1 goes into the WOL state (Dissidia 012).
	char groupName[ADHOCCTL_GROUPNAME_LEN];
	u32_le timeoutSec;  // How long to look once awake (0x3c in Legend of the Dragon, 0x14 in Dissidia 012).
	u32_le result;  // AdhocDiscoverResult or an adhocctl error, written when it finishes.
} PACK SceNetAdhocDiscoverParam;

enum AdhocDiscoverStatus : int {
	NET_ADHOC_DISCOVER_STATUS_NONE = 0,
	NET_ADHOC_DISCOVER_STATUS_IN_PROGRESS = 1,
	NET_ADHOC_DISCOVER_STATUS_COMPLETED = 2,
};

enum AdhocDiscoverResult : int {
	NET_ADHOC_DISCOVER_RESULT_PEER_FOUND = 0,  // In the group with a peer (pspautotests net/adhoc/pair/discover).
	NET_ADHOC_DISCOVER_RESULT_CANCELED = 1,  // After a Stop (pspautotests net/adhoc/discover).
	NET_ADHOC_DISCOVER_RESULT_NO_PEER_FOUND = 2,  // The timeout passed.
	NET_ADHOC_DISCOVER_RESULT_ABORTED = 3,
};

enum class AdhocDataMode {
	P2P = 0,
	AemuPostoffice,
};

struct AdhocServerListEntry {
	std::string name;
	std::string host;
	std::string web;
	std::string ip;  // could be used as a fallback if DNS resolution fails. Currently unused.
	std::string discord;
	std::string location;
	std::string description;
	std::string dataJsonUrl;
	std::string statusXmlUrl;
	std::string statusWebUrl;
	bool hidden = false;
	AdhocDataMode mode = AdhocDataMode::P2P;
};

enum class AdhocLoadListMode {
	CacheOnlySync,
	AllSourcesAsync,
};

void AdhocLoadServerList(AdhocLoadListMode loadMode);
std::vector<AdhocServerListEntry> AdhocGetServerList(AdhocLoadListMode loadMode);
bool AdhocGetServerByHost(std::string_view host, AdhocServerListEntry *dest);  // CacheOnlySync is enforced.

class PointerWrap;

void Register_sceNetAdhoc();
void Register_sceNetAdhocDiscover();

// TODO: expose it via "sceNetAdhocctl.h"
void Register_sceNetAdhocctl();


void __NetAdhocInit();
void __NetAdhocShutdown();
void __NetAdhocDoState(PointerWrap &p);

// TODO: expose it via "sceNetAdhocctl.h"
void __UpdateAdhocctlHandlers(u32 flags, u32 error);
// Drops queued events of one kind that haven't been delivered yet.
void __DropAdhocctlEvents(u32 flags);
// Leaves the server's group without an event, after entering game mode timed out.
void NetAdhocctl_GameModeTimedOut();

bool __NetAdhocConnected();

// TODO: expose these via "sceNetAdhocctl.h"
// Called from netdialog (and from sceNetApctl)
// NOTE: use hleCall for sceNet* ones!
int sceNetAdhocctlGetState(u32 ptrToStatus);
int sceNetAdhocctlCreate(MIPSState *mips, const char * groupName);
int sceNetAdhocctlConnect(MIPSState *mips, const char* groupName);
int sceNetAdhocctlJoin(MIPSState *mips, u32 scanInfoAddr);
int sceNetAdhocctlScan(MIPSState *mips);
int sceNetAdhocctlGetScanInfo(MIPSState *mips, u32 sizeAddr, u32 bufAddr);
int sceNetAdhocctlDisconnect();
int sceNetAdhocctlInit(MIPSState *mips, int stackSize, int prio, u32 productAddr);
int sceNetAdhocctlTerm();

int NetAdhocctl_Term();
int NetAdhocctl_GetState();
int NetAdhocctl_Create(const char* groupName);
int NetAdhocDiscover_GetStatus();
void Register_sceNetAdhocctl_lib();
void Register_sceNetIfhandle_lib();

int NetAdhoc_Term();

// May need to use these from sceNet.cpp
extern bool netAdhocInited; // TODO: keep here
extern bool netAdhocctlInited; // TODO: move to sceNetAdhocctl

extern bool netAdhocGameModeEntered;
extern bool netAdhocUseUdp;

// Exposing those for the matching routines
// NOTE: use hleCall for sceNet* ones!
int sceNetAdhocPdpSend(MIPSState *mips, int id, const char* mac, u32 port, void* data, int len, int timeout, int flag);
int sceNetAdhocPdpRecv(MIPSState *mips, int id, void* addr, void* port, void* buf, void* dataLength, u32 timeout, int flag);
int sceNetAdhocPdpCreate(MIPSState *mips, const char* mac, int port, int bufferSize, u32 flag);

int NetAdhoc_SetSocketAlert(int id, s32_le flag);
int NetAdhocPdp_Delete(int id, int unknown);
int NetAdhocPdpCreate(const char *mac, int port, int bufferSize, u32 flag, bool postoffice_enet_pdp_reliable);
