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

#include "Common/ChunkFile.h"
#include "HLE.h"
#include "../MIPS/MIPS.h"

#include "sceKernel.h"
#include "sceKernelThread.h"
#include "sceUtility.h"

static bool netInited;
static bool netInetInited;
static bool netAdhocInited;
static bool netApctlInited;

static u32 adhocctlHandlerCount;
static u32 apctlHandlerCount;

// TODO: Determine how many handlers we can actually have
const u32 MAX_ADHOCCTL_HANDLERS = 32;
const u32 MAX_APCTL_HANDLERS = 32;

enum {
	ERROR_NET_BUFFER_TOO_SMALL                   = 0x80400706,

	ERROR_NET_RESOLVER_BAD_ID                    = 0x80410408,
	ERROR_NET_RESOLVER_ALREADY_STOPPED           = 0x8041040a,
	ERROR_NET_RESOLVER_INVALID_HOST              = 0x80410414,

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

	ERROR_NET_ADHOCCTL_WLAN_SWITCH_OFF			 = 0x80410b03,
	ERROR_NET_ADHOCCTL_ALREADY_INITIALIZED       = 0x80410b07,
	ERROR_NET_ADHOCCTL_NOT_INITIALIZED           = 0x80410b08,
	ERROR_NET_ADHOCCTL_DISCONNECTED				 = 0x80410b09,
	ERROR_NET_ADHOCCTL_BUSY                      = 0x80410b10,
	ERROR_NET_ADHOCCTL_TOO_MANY_HANDLERS         = 0x80410b12,
};

// These might come in handy in the future, if PPSSPP ever supports wifi/ad-hoc..
struct SceNetAdhocctlParams {
	int channel; //which ad-hoc channel to connect to
	char name[8]; //connection name
	u8 bssid[6];  //BSSID of the connection?
	char nickname[128]; //PSP's nickname?
};

struct ProductStruct {
	int unknown; // Unknown, set to 0
	char product[9]; // Game ID (Example: ULUS10000)
};

struct SceNetMallocStat {
	int pool; // Pointer to the pool?
	int maximum; // Maximum size of the pool?
	int free; // How much memory is free
};

static struct SceNetMallocStat netMallocStat;

struct AdhocctlHandler {
	u32 entryPoint;
	u32 argument;
};

static std::map<int, AdhocctlHandler> adhocctlHandlers;

struct ApctlHandler {
	u32 entryPoint;
	u32 argument;
};

static std::map<int, ApctlHandler> apctlHandlers;

void __NetInit() {
	netInited = false;
	netAdhocInited = false;
	netApctlInited = false;
	netInetInited = false;
	memset(&netMallocStat, 0, sizeof(netMallocStat));
}

void __NetShutdown() {

}

void __UpdateAdhocctlHandlers(int flag, int error) {
	u32 args[3] = { 0, 0, 0 };
	args[0] = flag;
	args[1] = error;

	for(std::map<int, AdhocctlHandler>::iterator it = adhocctlHandlers.begin(); it != adhocctlHandlers.end(); ++it) {
		args[2] = it->second.argument;

		__KernelDirectMipsCall(it->second.entryPoint, NULL, args, 3, true);
	}
}

void __UpdateApctlHandlers(int oldState, int newState, int flag, int error) {
	u32 args[5] = { 0, 0, 0, 0, 0 };
		args[0] = oldState;
		args[1] = newState;
		args[2] = flag;
		args[3] = error;

	for(std::map<int, ApctlHandler>::iterator it = apctlHandlers.begin(); it != apctlHandlers.end(); ++it) {
		args[4] = it->second.argument;

		__KernelDirectMipsCall(it->second.entryPoint, NULL, args, 5, true);
	}
}

// This feels like a dubious proposition, mostly...
void __NetDoState(PointerWrap &p) {
	p.Do(netInited);
	p.Do(netInetInited);
	p.Do(netAdhocInited);
	p.Do(netApctlInited);
	p.Do(adhocctlHandlers);
	p.Do(adhocctlHandlerCount);
	p.Do(apctlHandlers);
	p.Do(apctlHandlerCount);
	p.Do(netMallocStat);
	p.DoMarker("net");
}

// TODO: should that struct actually be initialized here?
void sceNetInit() {
	ERROR_LOG(HLE,"UNIMPL sceNetInit(poolsize=%d, calloutpri=%i, calloutstack=%d, netintrpri=%i, netintrstack=%d)", PARAM(0), PARAM(1), PARAM(2), PARAM(3), PARAM(4));
	netInited = true;
	netMallocStat.maximum = PARAM(0);
	netMallocStat.free = PARAM(0);
	netMallocStat.pool = 0;

	RETURN(0);
}

u32 sceNetTerm() {
	ERROR_LOG(HLE,"UNIMPL sceNetTerm()");
	netInited = false;
	return 0;
}

u32 sceNetAdhocInit() {
	ERROR_LOG(HLE,"UNIMPL sceNetAdhocInit()");
	if (netAdhocInited)
		return ERROR_NET_ADHOC_ALREADY_INITIALIZED;
	netAdhocInited = true;

	return 0;
}

u32 sceNetAdhocctlInit(int stackSize, int prio, u32 productAddr) {
	ERROR_LOG(HLE,"UNIMPL sceNetAdhocctlInit(%i, %i, %08x)", stackSize, prio, productAddr);
	return 0;
}

u32 sceWlanGetEtherAddr(u32 addrAddr) {
	static const u8 fakeEtherAddr[6] = { 1, 2, 3, 4, 5, 6 };
	DEBUG_LOG(HLE, "sceWlanGetEtherAddr(%08x)", addrAddr);
	for (int i = 0; i < 6; i++)
		Memory::Write_U8(fakeEtherAddr[i], addrAddr + i);

	return 0;
}

u32 sceWlanDevIsPowerOn() {
	DEBUG_LOG(HLE, "UNTESTED 0=sceWlanDevIsPowerOn()");
	return 0;
}

u32 sceWlanGetSwitchState() {
	DEBUG_LOG(HLE, "UNTESTED sceWlanGetSwitchState()");
	return 0;
}

// TODO: How many handlers can the PSP actually have for Adhocctl?
// TODO: Should we allow the same handler to be added more than once?
u32 sceNetAdhocctlAddHandler(u32 handlerPtr, u32 handlerArg) {
	bool foundHandler = false;
	u32 retval = adhocctlHandlerCount;
	struct AdhocctlHandler handler;
	memset(&handler, 0, sizeof(handler));

	handler.entryPoint = handlerPtr;
	handler.argument = handlerArg;

	for(std::map<int, AdhocctlHandler>::iterator it = adhocctlHandlers.begin(); it != adhocctlHandlers.end(); it++) {
		if(it->second.entryPoint == handlerPtr) {
			foundHandler = true;
			break;
		}
	}

	if(!foundHandler && Memory::IsValidAddress(handlerPtr)) {
		if(adhocctlHandlerCount >= MAX_ADHOCCTL_HANDLERS) {
			ERROR_LOG(HLE, "UNTESTED UNTESTED sceNetAdhocctlAddHandler(%x, %x): Too many handlers", handlerPtr, handlerArg);
			retval = ERROR_NET_ADHOCCTL_TOO_MANY_HANDLERS;
			return retval;
		}
		adhocctlHandlers[adhocctlHandlerCount++] = handler;
		WARN_LOG(HLE, "UNTESTED sceNetAdhocctlAddHandler(%x, %x): added handler %d", handlerPtr, handlerArg, adhocctlHandlerCount);
	}
	else
		ERROR_LOG(HLE, "UNTESTED sceNetAdhocctlAddHandler(%x, %x): Same handler already exists", handlerPtr, handlerArg);


	// The id to return is the number of handlers currently registered
	return retval;
}

u32 sceNetAdhocctlDisconnect() {
	ERROR_LOG(HLE, "UNIMPL sceNetAdhocctlDisconnect()");
	__UpdateAdhocctlHandlers(0, ERROR_NET_ADHOCCTL_WLAN_SWITCH_OFF);

	return 0;
}

u32 sceNetAdhocctlDelHandler(u32 handlerID) {
	
	if(adhocctlHandlers.find(handlerID) != adhocctlHandlers.end()) {
		adhocctlHandlers.erase(handlerID);
		adhocctlHandlerCount = adhocctlHandlerCount > 0? --adhocctlHandlerCount : 0;
		WARN_LOG(HLE, "UNTESTED sceNetAdhocctlDelHandler(%d): deleted handler %d", handlerID, handlerID);
	}
	else
		ERROR_LOG(HLE, "UNTESTED sceNetAdhocctlDelHandler(%d): asked to delete invalid handler %d", handlerID, handlerID);

	return 0;
}

int sceNetAdhocMatchingTerm() {
	ERROR_LOG(HLE, "UNIMPL sceNetAdhocMatchingTerm()");
	return 0;
}

int sceNetAdhocctlTerm() {
	ERROR_LOG(HLE, "UNIMPL sceNetAdhocctlTerm()");
	return 0;
}

int sceNetAdhocTerm() {
	ERROR_LOG(HLE, "UNIMPL sceNetAdhocTerm()");
	return 0;
}

// Homebrew SDK claims it's a void function, but tests seem to indicate otherwise
int sceNetEtherNtostr(const char *mac, u32 bufferPtr) {
	DEBUG_LOG(HLE, "UNTESTED sceNetEtherNtostr(%s, %x)", mac, bufferPtr);
	if(Memory::IsValidAddress(bufferPtr)) {
		int len = strlen(mac);
		for (int i = 0; i < len; i++)
			Memory::Write_U8(mac[i], bufferPtr + i);
	}
	else
		ERROR_LOG(HLE, "UNTESTED sceNetEtherNtostr(%s, %x): Tried to write to an invalid pointer", mac, bufferPtr);

	return 0;
}

// Seems to always return 0, and write 0 to the pointer..
// TODO: Eventually research what possible states there are
int sceNetAdhocctlGetState(u32 ptrToStatus) {
	WARN_LOG(HLE, "UNTESTED sceNetAdhocctlGetState(%x)", ptrToStatus);
	if(Memory::IsValidAddress(ptrToStatus))
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
	for(int i = 0; i < 6; i++)
		params.bssid[i] = i + 1;
	strcpy(params.name, "");
	strcpy(params.nickname, "");

	if(Memory::IsValidAddress(paramAddr))
		Memory::WriteStruct(paramAddr, &params);

	return ERROR_NET_ADHOCCTL_DISCONNECTED;
}

// Return -1 packets since we don't have networking yet..
int sceNetAdhocPdpRecv(int id, const char *mac, u32 port, void *data, void *dataLength, u32 timeout, int nonBlock) {
	ERROR_LOG(HLE, "UNIMPL sceNetAdhocPdpRecv(%d, %d, %d, %x, %x, %d, %d)", id, mac, port, data, dataLength, timeout, nonBlock);
	return -1;
}

// Assuming < 0 for failure, homebrew SDK doesn't have much to say about this one..
int sceNetAdhocSetSocketAlert(int id, int flag) {
	ERROR_LOG(HLE, "UNIMPL sceNetAdhocSetSocketAlert(%d, %d)", id, flag);
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

int sceNetAdhocctlConnect(u32 ptrToGroupName) {
	ERROR_LOG(HLE, "UNIMPL sceNetAdhocctlConnect(%x)", ptrToGroupName);
	__UpdateAdhocctlHandlers(0, ERROR_NET_ADHOCCTL_WLAN_SWITCH_OFF);

	return 0;
}

// Write static data since we don't actually manage any memory for sceNet* yet.
int sceNetGetMallocStat(u32 statPtr) {
	WARN_LOG(HLE, "UNTESTED sceNetGetMallocStat(%x)", statPtr);
	if(Memory::IsValidAddress(statPtr))
		Memory::WriteStruct(statPtr, &netMallocStat);
	else
		ERROR_LOG(HLE, "UNTESTED sceNetGetMallocStat(%x): tried to request invalid address!", statPtr);

	return 0;
}

int sceNetAdhocMatchingInit(u32 memsize) {
	ERROR_LOG(HLE, "UNIMPL sceNetAdhocMatchingInit(%08x)", memsize);
	return 0;
}

int sceNetInetInit() {
	ERROR_LOG(HLE, "UNIMPL sceNetInetInit()");
	if (netInetInited)
		return ERROR_NET_ADHOC_ALREADY_INITIALIZED; // TODO: What's the proper error for netInet already being inited?
	netInetInited = true;

	return 0;
}

int sceNetInetTerm() {
	ERROR_LOG(HLE, "UNIMPL sceNetInetTerm()");
	netInetInited = false;

	return 0;
}

int sceNetApctlInit() {
	ERROR_LOG(HLE, "UNIMPL sceNetApctlInit()");
	if (netAdhocInited)
		return ERROR_NET_ADHOC_ALREADY_INITIALIZED; // TODO: What's the proper error for apctl already being inited?
	netApctlInited = true;

	return 0;
}

int sceNetApctlTerm() {
	ERROR_LOG(HLE, "UNIMPL sceNeApctlTerm()");
	netInetInited = false;

	return 0;
}

// TODO: How many handlers can the PSP actually have for Apctl?
// TODO: Should we allow the same handler to be added more than once?
u32 sceNetApctlAddHandler(u32 handlerPtr, u32 handlerArg) {
	bool foundHandler = false;
	u32 retval = apctlHandlerCount;
	struct ApctlHandler handler;
	memset(&handler, 0, sizeof(handler));

	handler.entryPoint = handlerPtr;
	handler.argument = handlerArg;

	for(std::map<int, ApctlHandler>::iterator it = apctlHandlers.begin(); it != apctlHandlers.end(); it++) {
		if(it->second.entryPoint == handlerPtr) {
			foundHandler = true;
			break;
		}
	}

	if(!foundHandler && Memory::IsValidAddress(handlerPtr)) {
		if(apctlHandlerCount >= MAX_APCTL_HANDLERS) {
			ERROR_LOG(HLE, "UNTESTED sceNetApctlAddHandler(%x, %x): Too many handlers", handlerPtr, handlerArg);
			retval = ERROR_NET_ADHOCCTL_TOO_MANY_HANDLERS; // TODO: What's the proper error code for Apctl's TOO_MANY_HANDLERS?
			return retval;
		}
		apctlHandlers[apctlHandlerCount++] = handler;
		WARN_LOG(HLE, "UNTESTED sceNetApctlAddHandler(%x, %x): added handler %d", handlerPtr, handlerArg, apctlHandlerCount);
	}
	else
		ERROR_LOG(HLE, "UNTESTED sceNetApctlAddHandler(%x, %x): Same handler already exists", handlerPtr, handlerArg);


	// The id to return is the number of handlers currently registered
	return retval;
}

int sceNetApctlDelHandler(u32 handlerID) {
	
	if(apctlHandlers.find(handlerID) != apctlHandlers.end()) {
		apctlHandlers.erase(handlerID);
		apctlHandlerCount = apctlHandlerCount > 0? --apctlHandlerCount : 0;
		WARN_LOG(HLE, "UNTESTED sceNetapctlDelHandler(%d): deleted handler %d", handlerID, handlerID);
	}
	else
		ERROR_LOG(HLE, "UNTESTED sceNetapctlDelHandler(%d): asked to delete invalid handler %d", handlerID, handlerID);

	return 0;
}



const HLEFunction sceNet[] = {
	{0x39AF39A6, sceNetInit, "sceNetInit"},
	{0x281928A9, WrapU_V<sceNetTerm>, "sceNetTerm"},
	{0x89360950, WrapI_CU<sceNetEtherNtostr>, "sceNetEtherNtostr"}, 
	{0x0bf0a3ae, 0, "sceNetGetLocalEtherAddr"}, 
	{0xd27961c9, 0, "sceNetEtherStrton"}, 
	{0x50647530, 0, "sceNetFreeThreadinfo"}, 
	{0xcc393e48, WrapI_U<sceNetGetMallocStat>, "sceNetGetMallocStat"},
	{0xad6844c6, 0, "sceNetThreadAbort"},
};

const HLEFunction sceNetAdhoc[] = {
	{0xE1D621D7, WrapU_V<sceNetAdhocInit>, "sceNetAdhocInit"}, 
	{0xA62C6F57, WrapI_V<sceNetAdhocTerm>, "sceNetAdhocTerm"}, 
	{0x0AD043ED, 0, "sceNetAdhocctlConnect"},
	{0x6f92741b, WrapI_CUIU<sceNetAdhocPdpCreate>, "sceNetAdhocPdpCreate"},
	{0xabed3790, 0, "sceNetAdhocPdpSend"},
	{0xdfe53e03, WrapI_ICUVVUI<sceNetAdhocPdpRecv>, "sceNetAdhocPdpRecv"},
	{0x7f27bb5e, WrapI_II<sceNetAdhocPdpDelete>, "sceNetAdhocPdpDelete"},
	{0xc7c1fc57, 0, "sceNetAdhocGetPdpStat"},
	{0x157e6225, 0, "sceNetAdhocPtpClose"},
	{0x4da4c788, 0, "sceNetAdhocPtpSend"},
	{0x877f6d66, 0, "sceNetAdhocPtpOpen"},
	{0x8bea2b3e, 0, "sceNetAdhocPtpRecv"},
	{0x9df81198, 0, "sceNetAdhocPtpAccept"},
	{0xe08bdac1, 0, "sceNetAdhocPtpListen"},
	{0xfc6fc07b, 0, "sceNetAdhocPtpConnect"},
	{0x9ac2eeac, 0, "sceNetAdhocPtpFlush"},
	{0xb9685118, 0, "sceNetAdhocGetPtpStat"},
	{0x3278ab0c, 0, "sceNetAdhocGameModeCreateReplica"},
	{0x98c204c8, 0, "sceNetAdhocGameModeUpdateMaster"}, 
	{0xfa324b4e, 0, "sceNetAdhocGameModeUpdateReplica"},
	{0xa0229362, 0, "sceNetAdhocGameModeDeleteMaster"},
	{0x0b2228e9, 0, "sceNetAdhocGameModeDeleteReplica"},
	{0x7F75C338, 0, "sceNetAdhocGameModeCreateMaster"},
	{0x73bfd52d, WrapI_II<sceNetAdhocSetSocketAlert>, "sceNetAdhocSetSocketAlert"},
	{0x7a662d6b, 0, "sceNetAdhocPollSocket"},
	{0x4d2ce199, 0, "sceNetAdhocGetSocketAlert"},
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

const HLEFunction sceNetResolver[] = {
	{0x224c5f44, 0, "sceNetResolverStartNtoA"},
	{0x244172af, 0, "sceNetResolverCreate"},
	{0x94523e09, 0, "sceNetResolverDelete"},
	{0xf3370e61, 0, "sceNetResolverInit"},
	{0x808F6063, 0, "sceNetResolverStop"},
	{0x6138194A, 0, "sceNetResolverTerm"},
	{0x629e2fb7, 0, "sceNetResolverStartAtoN"},
	{0x14c17ef9, 0, "sceNetResolverStartNtoAAsync"},
	{0xaac09184, 0, "sceNetResolverStartAtoNAsync"},
	{0x12748eb9, 0, "sceNetResolverWaitAsync"},
	{0x4ee99358, 0, "sceNetResolverPollAsync"},
};					 

const HLEFunction sceNetInet[] = {
	{0x17943399, WrapI_V<sceNetInetInit>, "sceNetInetInit"},
	{0x2fe71fe7, 0, "sceNetInetSetsockopt"},
	{0x410b34aa, 0, "sceNetInetConnect"},
	{0x5be8d595, 0, "sceNetInetSelect"},
	{0x7aa671bc, 0, "sceNetInetSend"},
	{0x8b7b220f, 0, "sceNetInetSocket"},
	{0x8d7284ea, 0, "sceNetInetClose"},
	{0xb75d5b0a, 0, "sceNetInetInetAddr"},
	{0xcda85c99, 0, "sceNetInetRecv"},
	{0xfbabe411, 0, "sceNetInetGetErrno"},
	{0x05038fc7, 0, "sceNetInetSendto"},
	{0x1a33f9ae, 0, "sceNetInetBind"},
	{0x4cfe4e56, 0, "sceNetInetShutdown"},
	{0xb3888ad4, 0, "sceNetInetGetTcpcbstat"},
	{0xc91142e4, 0, "sceNetInetRecvfrom"},
	{0xd0792666, 0, "sceNetInetInetNtop"},
	{0xd10a1a7a, 0, "sceNetInetListen"},
	{0xdb094e1b, 0, "sceNetInetAccept"},
	{0x8ca3a97e, 0, "sceNetInetGetPspError"},
	{0xa9ed66b9, WrapI_V<sceNetInetTerm>, "sceNetInetTerm"},
	{0xE30B8C19, 0, "sceNetInetInetPton"},
	{0xE247B6D6, 0, "sceNetInetGetpeername"},
	{0x162e6fd5, 0, "sceNetInetGetsockname"},
	{0x4a114c7c, 0, "sceNetInetGetsockopt"}, 
	{0xfaabb1dd, 0, "sceNetInetPoll"},
	{0x1BDF5D13, 0, "sceNetInetInetAton"},
	{0x80A21ABD, 0, "sceNetInetSocketAbort"},
	{0x805502DD, 0, "sceNetInetCloseWithRST"},
	{0x774e36f4, 0, "sceNetInetSendmsg"},
	{0xeece61d2, 0, "sceNetInetRecvmsg"},
	{0x39b0c7d3, 0, "sceNetInetGetUdpcbstat"},
};

const HLEFunction sceNetApctl[] = {
	{0xCFB957C6, 0, "sceNetApctlConnect"},
	{0x24fe91a1, 0, "sceNetApctlDisconnect"},
	{0x5deac81b, 0, "sceNetApctlGetState"},
	{0x8abadd51, WrapU_UU<sceNetApctlAddHandler>, "sceNetApctlAddHandler"},
	{0xe2f91f9b, WrapI_V<sceNetApctlInit>, "sceNetApctlInit"},
	{0x5963991b, WrapI_U<sceNetApctlDelHandler>, "sceNetApctlDelHandler"},
	{0xb3edd0ec, WrapI_V<sceNetApctlTerm>, "sceNetApctlTerm"},
	{0x2BEFDF23, 0, "sceNetApctlGetInfo"},
	{0xa3e77e13, 0, "sceNetApctlScanSSID2"},
	{0xf25a5006, 0, "sceNetApctlGetBSSDescIDList2"},
	{0x2935c45b, 0, "sceNetApctlGetBSSDescEntry2"},
};

const HLEFunction sceWlanDrv[] = {
	{0xd7763699, WrapU_V<sceWlanGetSwitchState>, "sceWlanGetSwitchState"},
	{0x0c622081, WrapU_U<sceWlanGetEtherAddr>, "sceWlanGetEtherAddr"},
	{0x93440B11, WrapU_V<sceWlanDevIsPowerOn>, "sceWlanDevIsPowerOn"},
};

void Register_sceNet() {
	RegisterModule("sceNet", ARRAY_SIZE(sceNet), sceNet);
	RegisterModule("sceNetAdhoc", ARRAY_SIZE(sceNetAdhoc), sceNetAdhoc);
	RegisterModule("sceNetAdhocMatching", ARRAY_SIZE(sceNetAdhocMatching), sceNetAdhocMatching);
	RegisterModule("sceNetAdhocctl", ARRAY_SIZE(sceNetAdhocctl), sceNetAdhocctl);
	RegisterModule("sceNetResolver", ARRAY_SIZE(sceNetResolver), sceNetResolver);
	RegisterModule("sceNetInet", ARRAY_SIZE(sceNetInet), sceNetInet);
	RegisterModule("sceNetApctl", ARRAY_SIZE(sceNetApctl), sceNetApctl);
}

void Register_sceWlanDrv() {
	RegisterModule("sceWlanDrv", ARRAY_SIZE(sceWlanDrv), sceWlanDrv);
}
