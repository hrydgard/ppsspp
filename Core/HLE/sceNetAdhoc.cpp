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


// sceNetAdhoc

// These acronyms are seen in function names:
// * PDP: a proprietary Sony protocol similar to UDP.
// * PTP: a proprietary Sony protocol similar to TCP.

// We will need to wrap them into the similar UDP and TCP messages. If we want to
// play adhoc remotely online, I guess we'll need to wrap both into TCP/IP.

// We will need some server infrastructure to provide match making. We'll
// group players per game. Maybe allow players to join rooms and then start the game,
// instead of the other way around?


#include "Core/HLE/HLE.h"
#include "Core/HLE/sceNetAdhoc.h"

#include "sceKernel.h"
#include "sceKernelThread.h"
#include "sceKernelMutex.h"
#include "sceUtility.h"

#include "net/resolve.h"

enum {
	ERROR_NET_ADHOC_INVALID_SOCKET_ID            = 0x80410701,
	ERROR_NET_ADHOC_INVALID_ADDR                 = 0x80410702,
	ERROR_NET_ADHOC_NO_DATA_AVAILABLE            = 0x80410709,
	ERROR_NET_ADHOC_PORT_IN_USE                  = 0x8041070a,
	ERROR_NET_ADHOC_NOT_INITIALIZED              = 0x80410712,
	ERROR_NET_ADHOC_ALREADY_INITIALIZED          = 0x80410713,
	ERROR_NET_ADHOC_DISCONNECTED                 = 0x8041070c,
	ERROR_NET_ADHOC_TIMEOUT                      = 0x80410715,
	ERROR_NET_ADHOC_NO_ENTRY                     = 0x80410716,
	ERROR_NET_ADHOC_CONNECTION_REFUSED           = 0x80410718,
	ERROR_NET_ADHOC_INVALID_MATCHING_ID          = 0x80410807,
	ERROR_NET_ADHOC_MATCHING_ALREADY_INITIALIZED = 0x80410812,
	ERROR_NET_ADHOC_MATCHING_NOT_INITIALIZED     = 0x80410813,

	ERROR_NET_ADHOCCTL_WLAN_SWITCH_OFF           = 0x80410b03,
	ERROR_NET_ADHOCCTL_ALREADY_INITIALIZED       = 0x80410b07,
	ERROR_NET_ADHOCCTL_NOT_INITIALIZED           = 0x80410b08,
	ERROR_NET_ADHOCCTL_DISCONNECTED              = 0x80410b09,
	ERROR_NET_ADHOCCTL_BUSY                      = 0x80410b10,
	ERROR_NET_ADHOCCTL_TOO_MANY_HANDLERS         = 0x80410b12,
};

enum {
	PSP_ADHOC_POLL_READY_TO_SEND = 1,
	PSP_ADHOC_POLL_DATA_AVAILABLE = 2,
	PSP_ADHOC_POLL_CAN_CONNECT = 4,
	PSP_ADHOC_POLL_CAN_ACCEPT = 8,
};

const size_t MAX_ADHOCCTL_HANDLERS = 32;

static bool netAdhocInited;
static bool netAdhocctlInited;
static bool netAdhocMatchingInited;

// These might come in handy in the future, if PPSSPP ever supports wifi/ad-hoc..
struct SceNetAdhocctlParams {
	s32_le channel; //which ad-hoc channel to connect to
	char name[8]; //connection name
	u8 bssid[6];  //BSSID of the connection?
	char nickname[128]; //PSP's nickname?
};

struct AdhocctlHandler {
	u32 entryPoint;
	u32 argument;
};

static std::map<int, AdhocctlHandler> adhocctlHandlers;

void __NetAdhocInit() {
	netAdhocInited = false;
	netAdhocctlInited = false;
	netAdhocMatchingInited = false;
	adhocctlHandlers.clear();
}

void __NetAdhocShutdown() {

}

void __NetAdhocDoState(PointerWrap &p) {
	p.Do(netAdhocInited);
	p.Do(netAdhocctlInited);
	p.Do(netAdhocMatchingInited);
	p.Do(adhocctlHandlers);
	p.DoMarker("netadhoc");
}

void __UpdateAdhocctlHandlers(int flag, int error) {
	u32 args[3] = { 0, 0, 0 };
	args[0] = flag;
	args[1] = error;

	for (std::map<int, AdhocctlHandler>::iterator it = adhocctlHandlers.begin(); it != adhocctlHandlers.end(); ++it) {
		args[2] = it->second.argument;

		__KernelDirectMipsCall(it->second.entryPoint, NULL, args, 3, true);
	}
}

u32 sceNetAdhocInit() {
	ERROR_LOG(HLE, "UNIMPL sceNetAdhocInit()");
	if (netAdhocInited)
		return ERROR_NET_ADHOC_ALREADY_INITIALIZED;
	netAdhocInited = true;

	return 0;
}

u32 sceNetAdhocctlInit(int stackSize, int prio, u32 productAddr) {
	ERROR_LOG(HLE, "UNIMPL sceNetAdhocctlInit(%i, %i, %08x)", stackSize, prio, productAddr);
	if (netAdhocctlInited)
		return ERROR_NET_ADHOCCTL_ALREADY_INITIALIZED;
	netAdhocctlInited = true;

	return 0;
}



// Seems to always return 0, and write 0 to the pointer..
// TODO: Eventually research what possible states there are
int sceNetAdhocctlGetState(u32 ptrToStatus) {
	WARN_LOG(HLE, "UNTESTED sceNetAdhocctlGetState(%x)", ptrToStatus);
	if (Memory::IsValidAddress(ptrToStatus))
		Memory::Write_U32(0, ptrToStatus);
	else
		ERROR_LOG(HLE, "UNTESTED sceNetAdhocctlGetState(%x): Tried to write invalid location", ptrToStatus);

	return 0;
}

// Always return -1 since we don't have any real networking...
int sceNetAdhocPdpCreate(const char *mac, u32 port, int bufferSize, u32 unknown) {
	ERROR_LOG(HLE, "UNIMPL sceNetAdhocPdpCreate(%s, %x, %x, %x)", mac, port, bufferSize, unknown);
	return -1;
}

// TODO: Should we really write the struct if we're disconnected?
int sceNetAdhocctlGetParameter(u32 paramAddr) {
	WARN_LOG(HLE, "UNTESTED sceNetAdhocctlGetParameter(%x)", paramAddr);
	struct SceNetAdhocctlParams params;
	params.channel = 0;
	for (int i = 0; i < 6; i++)
		params.bssid[i] = i + 1;
	strcpy(params.name, "");
	strcpy(params.nickname, "");

	if (Memory::IsValidAddress(paramAddr))
		Memory::WriteStruct(paramAddr, &params);

	return ERROR_NET_ADHOCCTL_DISCONNECTED;
}

int sceNetAdhocPdpSend(int id, const char *mac, u32 port, void *data, void *dataLength, u32 timeout, int nonBlock) {
	ERROR_LOG(HLE, "UNIMPL sceNetAdhocPdpSend(%d, %s, %d, %p, %p, %d, %d)", id, mac, port, data, dataLength, timeout, nonBlock);
	return -1;
}

// Return -1 packets since we don't have networking yet..
int sceNetAdhocPdpRecv(int id, const char *mac, u32 port, void *data, void *dataLength, u32 timeout, int nonBlock) {
	ERROR_LOG(HLE, "UNIMPL sceNetAdhocPdpRecv(%d, %s, %d, %p, %p, %d, %d)", id, mac, port, data, dataLength, timeout, nonBlock);
	return -1;
}

// Assuming < 0 for failure, homebrew SDK doesn't have much to say about this one..
int sceNetAdhocSetSocketAlert(int id, int flag) {
	ERROR_LOG(HLE, "UNIMPL sceNetAdhocSetSocketAlert(%d, %d)", id, flag);
	return -1;
}

int sceNetAdhocPollSocket(u32 socketStructAddr, int count, int timeout, int nonblock) {
	ERROR_LOG(HLE, "UNIMPL sceNetAdhocPollSocket(%08x, %i, %i, %i)", count, timeout, nonblock);
	return -1;
}


int sceNetAdhocPdpDelete(int id, int unknown) {
	ERROR_LOG(HLE, "UNIMPL sceNetAdhocPdpDelete(%d, %d)", id, unknown);
	return 0;
}

int sceNetAdhocctlGetAdhocId(u32 productStructAddr) {
	ERROR_LOG(HLE, "UNIMPL sceNetAdhocctlGetAdhocId(%x)", productStructAddr);
	return 0;
}

int sceNetAdhocctlScan() {
	ERROR_LOG(HLE, "UNIMPL sceNetAdhocctlScan()");
	__UpdateAdhocctlHandlers(0, ERROR_NET_ADHOCCTL_WLAN_SWITCH_OFF);

	return 0;
}

int sceNetAdhocctlGetScanInfo() {
	ERROR_LOG(HLE, "UNIMPL sceNetAdhocctlGetScanInfo()");
	return 0;
}

// TODO: How many handlers can the PSP actually have for Adhocctl?
// TODO: Should we allow the same handler to be added more than once?
u32 sceNetAdhocctlAddHandler(u32 handlerPtr, u32 handlerArg) {
	bool foundHandler = false;
	u32 retval = 0;
	struct AdhocctlHandler handler;
	memset(&handler, 0, sizeof(handler));

	while (adhocctlHandlers.find(retval) != adhocctlHandlers.end())
		++retval;

	handler.entryPoint = handlerPtr;
	handler.argument = handlerArg;

	for (std::map<int, AdhocctlHandler>::iterator it = adhocctlHandlers.begin(); it != adhocctlHandlers.end(); it++) {
		if (it->second.entryPoint == handlerPtr) {
			foundHandler = true;
			break;
		}
	}

	if (!foundHandler && Memory::IsValidAddress(handlerPtr)) {
		if (adhocctlHandlers.size() >= MAX_ADHOCCTL_HANDLERS) {
			ERROR_LOG(HLE, "UNTESTED UNTESTED sceNetAdhocctlAddHandler(%x, %x): Too many handlers", handlerPtr, handlerArg);
			retval = ERROR_NET_ADHOCCTL_TOO_MANY_HANDLERS;
			return retval;
		}
		adhocctlHandlers[retval] = handler;
		WARN_LOG(HLE, "UNTESTED sceNetAdhocctlAddHandler(%x, %x): added handler %d", handlerPtr, handlerArg, retval);
	} else {
		ERROR_LOG(HLE, "UNTESTED sceNetAdhocctlAddHandler(%x, %x): Same handler already exists", handlerPtr, handlerArg);
	}

	// The id to return is the number of handlers currently registered
	return retval;
}

int sceNetAdhocctlConnect(u32 ptrToGroupName) {
	if (Memory::IsValidAddress(ptrToGroupName)) {
		ERROR_LOG(HLE, "UNIMPL sceNetAdhocctlConnect(groupName=%s)", Memory::GetCharPointer(ptrToGroupName));
	} else {
		ERROR_LOG(HLE, "UNIMPL sceNetAdhocctlConnect(%x)", ptrToGroupName);
	}
	__UpdateAdhocctlHandlers(0, ERROR_NET_ADHOCCTL_WLAN_SWITCH_OFF);

	return 0;
}

u32 sceNetAdhocctlDisconnect() {
	ERROR_LOG(HLE, "UNIMPL sceNetAdhocctlDisconnect()");
	__UpdateAdhocctlHandlers(0, ERROR_NET_ADHOCCTL_WLAN_SWITCH_OFF);

	return 0;
}

u32 sceNetAdhocctlDelHandler(u32 handlerID) {
	if (adhocctlHandlers.find(handlerID) != adhocctlHandlers.end()) {
		adhocctlHandlers.erase(handlerID);
		WARN_LOG(HLE, "UNTESTED sceNetAdhocctlDelHandler(%d): deleted handler %d", handlerID, handlerID);
	} else {
		ERROR_LOG(HLE, "UNTESTED sceNetAdhocctlDelHandler(%d): asked to delete invalid handler %d", handlerID, handlerID);
	}

	return 0;
}


int sceNetAdhocctlTerm() {
	ERROR_LOG(HLE, "UNIMPL sceNetAdhocctlTerm()");
	netAdhocctlInited = false;

	return 0;
}

int sceNetAdhocTerm() {
	ERROR_LOG(HLE, "UNIMPL sceNetAdhocTerm()");
	// Seems to return this when called a second time after being terminated without another initialisation
	if(!netAdhocInited) 
		return SCE_KERNEL_ERROR_LWMUTEX_NOT_FOUND;
	netAdhocInited = false;

	return 0;
}

int sceNetAdhocMatchingInit(u32 memsize) {
	ERROR_LOG(HLE, "UNIMPL sceNetAdhocMatchingInit(%08x)", memsize);
	if(netAdhocMatchingInited) 
		return ERROR_NET_ADHOC_MATCHING_ALREADY_INITIALIZED;
	netAdhocMatchingInited = true;

	return 0;
}

int sceNetAdhocMatchingTerm() {
	ERROR_LOG(HLE, "UNIMPL sceNetAdhocMatchingTerm()");
	netAdhocMatchingInited = false;

	return 0;
}

int sceNetAdhocGetPdpStat(int structSize, u32 structAddr) {
	ERROR_LOG(HLE, "UNIMPL sceNetAdhocGetPdpStat(%i, %08x)", structSize, structAddr);
	return 0;
}

int sceNetAdhocGetPtpStat(int structSize, u32 structAddr) {
	ERROR_LOG(HLE, "UNIMPL sceNetAdhocGetPtpStat(%i, %08x)", structSize, structAddr);
	return 0;
}

int sceNetAdhocPtpOpen(const char *srcmac, int srcport, const char *dstmac, int dstport, int bufsize, int retryDelay, int retryCount, int unknown) {
	ERROR_LOG(HLE, "UNIMPL sceNetAdhocPtpOpen(%s : %i, %s : %i, %i, %i, %i)", srcmac, srcport, dstmac, dstport, bufsize, retryDelay, retryCount, unknown);
	return 0;
}

int sceNetAdhocPtpAccept(int id, u32 peerMacAddrPtr, u32 peerPortPtr, int timeout, int nonblock) {
	ERROR_LOG(HLE, "UNIMPL sceNetAdhocPtpAccept(%i, %08x, %08x, %i, %i)", id, peerMacAddrPtr, peerPortPtr, timeout, nonblock);
	return 0;
}

int sceNetAdhocPtpConnect(int id, int timeout, int nonblock) {
	ERROR_LOG(HLE, "UNIMPL sceNetAdhocPtpConnect(%i, %i, %i)", id, timeout, nonblock);
	return -1;
}

int sceNetAdhocPtpClose(int id, int unknown) {
	ERROR_LOG(HLE, "UNIMPL sceNetAdhocPtpClose(%i, %i)", id, unknown);
	return 0;
}

int sceNetAdhocPtpListen(const char *srcmac, int srcport, int bufsize, int retryDelay, int retryCount, int queue, int unk) {
	ERROR_LOG(HLE, "UNIMPL sceNetAdhocPtpListen(%s : %i, %i, %i, %i, %i, %i)", srcmac, srcport, bufsize, retryDelay, retryCount, queue, unk);
	return 0;
}

int sceNetAdhocPtpSend(int id, u32 data, u32 dataSizeAddr, int timeout, int nonblock) {
	ERROR_LOG(HLE, "UNIMPL sceNetAdhocPtpSend(%i, %08x, %08x, %i, %i)", id, data, dataSizeAddr, timeout, nonblock);
	return 0;
}

int sceNetAdhocPtpRecv(int id, u32 data, u32 dataSizeAddr, int timeout, int nonblock) {
	ERROR_LOG(HLE, "UNIMPL sceNetAdhocPtpRecv(%i, %08x, %08x, %i, %i)", id, data, dataSizeAddr, timeout, nonblock);
	return 0;
}

int sceNetAdhocPtpFlush(int id, int timeout, int nonblock) {
	ERROR_LOG(HLE, "UNIMPL sceNetAdhocPtpFlush(%i, %i, %i)", id, timeout, nonblock);
	return 0;
}

int sceNetAdhocGameModeCreateMaster(u32 data, int size) {
	ERROR_LOG(HLE, "UNIMPL sceNetAdhocGameModeCreateMaster(%08x, %i)", data, size);
	return -1;
}

int sceNetAdhocGameModeCreateReplica(const char *mac, u32 data, int size) {
	ERROR_LOG(HLE, "UNIMPL sceNetAdhocGameModeCreateReplica(%s, %08x, %i)", mac, data, size);
	return -1;
}

int sceNetAdhocGameModeUpdateMaster() {
	ERROR_LOG(HLE, "UNIMPL sceNetAdhocGameModeUpdateMaster()");
	return -1;
}

int sceNetAdhocGameModeDeleteMaster() {
	ERROR_LOG(HLE, "UNIMPL sceNetAdhocGameModeDeleteMaster()");
	return -1;
}

int sceNetAdhocGameModeUpdateReplica(int id, u32 infoAddr) {
	ERROR_LOG(HLE, "UNIMPL sceNetAdhocGameModeUpdateReplica(%i, %08x)", id, infoAddr);
	return -1;
}

int sceNetAdhocGameModeDeleteReplica(int id) {
	ERROR_LOG(HLE, "UNIMPL sceNetAdhocGameModeDeleteReplica(%i)", id);
	return -1;
}

int sceNetAdhocGetSocketAlert() {
	ERROR_LOG(HLE, "UNIMPL sceNetAdhocGetSocketAlert()");
	return 0;
}

const HLEFunction sceNetAdhoc[] = {
	{0xE1D621D7, WrapU_V<sceNetAdhocInit>, "sceNetAdhocInit"}, 
	{0xA62C6F57, WrapI_V<sceNetAdhocTerm>, "sceNetAdhocTerm"}, 
	{0x0AD043ED, WrapI_U<sceNetAdhocctlConnect>, "sceNetAdhocctlConnect"},
	{0x6f92741b, WrapI_CUIU<sceNetAdhocPdpCreate>, "sceNetAdhocPdpCreate"},
	{0xabed3790, WrapI_ICUVVUI<sceNetAdhocPdpSend>, "sceNetAdhocPdpSend"},
	{0xdfe53e03, WrapI_ICUVVUI<sceNetAdhocPdpRecv>, "sceNetAdhocPdpRecv"},
	{0x7f27bb5e, WrapI_II<sceNetAdhocPdpDelete>, "sceNetAdhocPdpDelete"},
	{0xc7c1fc57, WrapI_IU<sceNetAdhocGetPdpStat>, "sceNetAdhocGetPdpStat"},
	{0x157e6225, WrapI_II<sceNetAdhocPtpClose>, "sceNetAdhocPtpClose"},
	{0x4da4c788, WrapI_IUUII<sceNetAdhocPtpSend>, "sceNetAdhocPtpSend"},
	{0x877f6d66, WrapI_CICIIIII<sceNetAdhocPtpOpen>, "sceNetAdhocPtpOpen"},
	{0x8bea2b3e, WrapI_IUUII<sceNetAdhocPtpRecv>, "sceNetAdhocPtpRecv"},
	{0x9df81198, WrapI_IUUII<sceNetAdhocPtpAccept>, "sceNetAdhocPtpAccept"},
	{0xe08bdac1, WrapI_CIIIIII<sceNetAdhocPtpListen>, "sceNetAdhocPtpListen"},
	{0xfc6fc07b, WrapI_III<sceNetAdhocPtpConnect>, "sceNetAdhocPtpConnect"},
	{0x9ac2eeac, WrapI_III<sceNetAdhocPtpFlush>, "sceNetAdhocPtpFlush"},
	{0xb9685118, WrapI_IU<sceNetAdhocGetPtpStat>, "sceNetAdhocGetPtpStat"},
	{0x3278ab0c, WrapI_CUI<sceNetAdhocGameModeCreateReplica>, "sceNetAdhocGameModeCreateReplica"},
	{0x98c204c8, WrapI_V<sceNetAdhocGameModeUpdateMaster>, "sceNetAdhocGameModeUpdateMaster"}, 
	{0xfa324b4e, WrapI_IU<sceNetAdhocGameModeUpdateReplica>, "sceNetAdhocGameModeUpdateReplica"},
	{0xa0229362, WrapI_V<sceNetAdhocGameModeDeleteMaster>, "sceNetAdhocGameModeDeleteMaster"},
	{0x0b2228e9, WrapI_I<sceNetAdhocGameModeDeleteReplica>, "sceNetAdhocGameModeDeleteReplica"},
	{0x7F75C338, WrapI_UI<sceNetAdhocGameModeCreateMaster>, "sceNetAdhocGameModeCreateMaster"},
	{0x73bfd52d, WrapI_II<sceNetAdhocSetSocketAlert>, "sceNetAdhocSetSocketAlert"},
	{0x4d2ce199, WrapI_V<sceNetAdhocGetSocketAlert>, "sceNetAdhocGetSocketAlert"},
	{0x7a662d6b, WrapI_UIII<sceNetAdhocPollSocket>, "sceNetAdhocPollSocket"},
};							

const HLEFunction sceNetAdhocMatching[] = {
	{0x2a2a1e07, WrapI_U<sceNetAdhocMatchingInit>, "sceNetAdhocMatchingInit"},
	{0x7945ecda, WrapI_V<sceNetAdhocMatchingTerm>, "sceNetAdhocMatchingTerm"},
	{0xca5eda6f, 0, "sceNetAdhocMatchingCreate"},
	{0x93ef3843, 0, "sceNetAdhocMatchingStart"},
	{0x32b156b3, 0, "sceNetAdhocMatchingStop"},
	{0xf16eaf4f, 0, "sceNetAdhocMatchingDelete"},
	{0x5e3d4b79, 0, "sceNetAdhocMatchingSelectTarget"},
	{0xea3c6108, 0, "sceNetAdhocMatchingCancelTarget"},
	{0x8f58bedf, 0, "sceNetAdhocMatchingCancelTargetWithOpt"},
	{0xb58e61b7, 0, "sceNetAdhocMatchingSetHelloOpt"},
	{0xc58bcd9e, 0, "sceNetAdhocMatchingGetMembers"},
	{0xec19337d, 0, "sceNetAdhocMatchingAbortSendData"},
	{0xf79472d7, 0, "sceNetAdhocMatchingSendData"},
	{0x40F8F435, 0, "sceNetAdhocMatchingGetPoolMaxAlloc"},
	{0xb5d96c2a, 0, "sceNetAdhocMatchingGetHelloOpt"},
	{0x9c5cfb7d, 0, "sceNetAdhocMatchingGetPoolStat"},
};

const HLEFunction sceNetAdhocctl[] = {
	{0xE26F226E, WrapU_IIU<sceNetAdhocctlInit>, "sceNetAdhocctlInit"},
	{0x9D689E13, WrapI_V<sceNetAdhocctlTerm>, "sceNetAdhocctlTerm"},
	{0x20B317A0, WrapU_UU<sceNetAdhocctlAddHandler>, "sceNetAdhocctlAddHandler"},
	{0x6402490B, WrapU_U<sceNetAdhocctlDelHandler>, "sceNetAdhocctlDelHandler"},
	{0x34401D65, WrapU_V<sceNetAdhocctlDisconnect>, "sceNetAdhocctlDisconnect"},
	{0x0ad043ed, WrapI_U<sceNetAdhocctlConnect>, "sceNetAdhocctlConnect"},
	{0x08fff7a0, WrapI_V<sceNetAdhocctlScan>, "sceNetAdhocctlScan"},
	{0x75ecd386, WrapI_U<sceNetAdhocctlGetState>, "sceNetAdhocctlGetState"},
	{0x8916c003, 0, "sceNetAdhocctlGetNameByAddr"},
	{0xded9d28e, WrapI_U<sceNetAdhocctlGetParameter>, "sceNetAdhocctlGetParameter"},
	{0x81aee1be, WrapI_V<sceNetAdhocctlGetScanInfo>, "sceNetAdhocctlGetScanInfo"},
	{0x5e7f79c9, 0, "sceNetAdhocctlJoin"},
	{0x8db83fdc, 0, "sceNetAdhocctlGetPeerInfo"},
	{0xec0635c1, 0, "sceNetAdhocctlCreate"},
	{0xa5c055ce, 0, "sceNetAdhocctlCreateEnterGameMode"},
	{0x1ff89745, 0, "sceNetAdhocctlJoinEnterGameMode"},
	{0xcf8e084d, 0, "sceNetAdhocctlExitGameMode"},
	{0xe162cb14, 0, "sceNetAdhocctlGetPeerList"},
	{0x362cbe8f, WrapI_U<sceNetAdhocctlGetAdhocId>, "sceNetAdhocctlGetAdhocId"},
	{0x5a014ce0, 0, "sceNetAdhocctlGetGameModeInfo"},
	{0x99560abe, 0, "sceNetAdhocctlGetAddrByName"},
	{0xb0b80e80, 0, "sceNetAdhocctlCreateEnterGameModeMin"},
};

const HLEFunction sceNetAdhocDiscover[] = {
	{0x941B3877, 0, "sceNetAdhocDiscoverInitStart"},
	{0x52DE1B97, 0, "sceNetAdhocDiscoverUpdate"},
	{0x944DDBC6, 0, "sceNetAdhocDiscoverGetStatus"},
	{0xA2246614, 0, "sceNetAdhocDiscoverTerm"},
	{0xF7D13214, 0, "sceNetAdhocDiscoverStop"},
	{0xA423A21B, 0, "sceNetAdhocDiscoverRequestSuspend"},
};

void Register_sceNetAdhoc() {
	RegisterModule("sceNetAdhoc", ARRAY_SIZE(sceNetAdhoc), sceNetAdhoc);
	RegisterModule("sceNetAdhocMatching", ARRAY_SIZE(sceNetAdhocMatching), sceNetAdhocMatching);
	RegisterModule("sceNetAdhocDiscover", ARRAY_SIZE(sceNetAdhocDiscover), sceNetAdhocDiscover);
	RegisterModule("sceNetAdhocctl", ARRAY_SIZE(sceNetAdhocctl), sceNetAdhocctl);
}
