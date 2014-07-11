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

#include "net/resolve.h"
#include "util/text/parsers.h"

#include "Common/ChunkFile.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/MIPS/MIPS.h"
#include "Core/Config.h"
#include "Core/MemMap.h"

#include "sceKernel.h"
#include "sceKernelThread.h"
#include "sceKernelMutex.h"
#include "sceUtility.h"

#include "Core/HLE/sceNetAdhoc.h"

static bool netInited;
static bool netInetInited;
static bool netApctlInited;

// TODO: Determine how many handlers we can actually have
const size_t MAX_APCTL_HANDLERS = 32;

enum {
	ERROR_NET_BUFFER_TOO_SMALL           = 0x80400706,

	ERROR_NET_INET_ALREADY_INITIALIZED   = 0x80410201,

	ERROR_NET_RESOLVER_BAD_ID            = 0x80410408,
	ERROR_NET_RESOLVER_ALREADY_STOPPED   = 0x8041040a,
	ERROR_NET_RESOLVER_INVALID_HOST      = 0x80410414,

	ERROR_NET_APCTL_ALREADY_INITIALIZED  = 0x80410a01,
	ERROR_NET_ADHOCCTL_TOO_MANY_HANDLERS = 0x80410b12,
};

enum {
	PSP_NET_APCTL_EVENT_DISCONNECT_REQUEST = 5,
};

struct ProductStruct {
	s32_le unknown; // Unknown, set to 0
	char product[9]; // Game ID (Example: ULUS10000)
};

struct SceNetMallocStat {
	s32_le pool; // Pointer to the pool?
	s32_le maximum; // Maximum size of the pool?
	s32_le free; // How much memory is free
};

static struct SceNetMallocStat netMallocStat;


struct ApctlHandler {
	u32 entryPoint;
	u32 argument;
};

static std::map<int, ApctlHandler> apctlHandlers;

void __ResetInitNetLib() {
	netInited = false;
	netApctlInited = false;
	netInetInited = false;

	memset(&netMallocStat, 0, sizeof(netMallocStat));
}

void __NetInit() {
	__ResetInitNetLib();
}

void __NetShutdown() {
	__ResetInitNetLib();
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
	auto s = p.Section("sceNet", 1);
	if (!s)
		return;

	p.Do(netInited);
	p.Do(netInetInited);
	p.Do(netApctlInited);
	p.Do(apctlHandlers);
	p.Do(netMallocStat);
}

u32 sceNetTerm() {
	//May also need to Terminate netAdhocctl and netAdhoc since the game (ie. GTA:VCS, Wipeout Pulse, etc) might not called them before calling sceNetTerm and causing them to behave strangely on the next sceNetInit+sceNetAdhocInit
	if (netAdhocctlInited) sceNetAdhocctlTerm();
	if (netAdhocInited) sceNetAdhocTerm();

	WARN_LOG(SCENET, "UNTESTED sceNetTerm()");
	netInited = false;
	return 0;
}

// TODO: should that struct actually be initialized here?
u32 sceNetInit(u32 poolSize, u32 calloutPri, u32 calloutStack, u32 netinitPri, u32 netinitStack)  {
	// May need to Terminate old one first since the game (ie. GTA:VCS) might not called sceNetTerm before the next sceNetInit and behave strangely
	if (netInited) sceNetTerm();

	ERROR_LOG(SCENET, "UNIMPL sceNetInit(poolsize=%d, calloutpri=%i, calloutstack=%d, netintrpri=%i, netintrstack=%d) at %08x", poolSize, calloutPri, calloutStack, netinitPri, netinitStack, currentMIPS->pc);
	netInited = true;
	netMallocStat.maximum = poolSize;
	netMallocStat.free = poolSize;
	netMallocStat.pool = 0;

	return 0;
}

u32 sceWlanGetEtherAddr(u32 addrAddr) {
  // Read MAC Address from config
	uint8_t mac[6] = {0};
	if (!ParseMacAddress(g_Config.localMacAddress.c_str(), mac)) {
		ERROR_LOG(SCENET, "Error parsing mac address %s", g_Config.localMacAddress.c_str());
	}
	DEBUG_LOG(SCENET, "sceWlanGetEtherAddr(%08x)", addrAddr);
	for (int i = 0; i < 6; i++)
		Memory::Write_U8(mac[i], addrAddr + i);
	return 0;
}

u32 sceNetGetLocalEtherAddr(u32 addrAddr) {
	return sceWlanGetEtherAddr(addrAddr);
}

u32 sceWlanDevIsPowerOn() {
	DEBUG_LOG(SCENET, "UNTESTED sceWlanDevIsPowerOn()");
	return g_Config.bEnableWlan ? 1 : 0;
}

u32 sceWlanGetSwitchState() {
	DEBUG_LOG(SCENET, "UNTESTED sceWlanGetSwitchState()");
	return g_Config.bEnableWlan ? 1 : 0;
}

// Probably a void function, but often returns a useful value.
int sceNetEtherNtostr(u32 macPtr, u32 bufferPtr) {
	DEBUG_LOG(SCENET, "sceNetEtherNtostr(%08x, %08x)", macPtr, bufferPtr);

	if (Memory::IsValidAddress(bufferPtr) && Memory::IsValidAddress(macPtr)) {
		char *buffer = (char *)Memory::GetPointer(bufferPtr);
		const u8 *mac = Memory::GetPointer(macPtr);

		// MAC address is always 6 bytes / 48 bits.
		return sprintf(buffer, "%02x:%02x:%02x:%02x:%02x:%02x",
			mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
	} else {
		// Possibly a void function, seems to return this on bad args.
		return 0x09d40000;
	}
}

static int hex_to_digit(int c) {
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

// Probably a void function, but sometimes returns a useful-ish value.
int sceNetEtherStrton(u32 bufferPtr, u32 macPtr) {
	DEBUG_LOG(SCENET, "sceNetEtherStrton(%08x, %08x)", bufferPtr, macPtr);

	if (Memory::IsValidAddress(bufferPtr) && Memory::IsValidAddress(macPtr)) {
		const char *buffer = (char *)Memory::GetPointer(bufferPtr);
		u8 *mac = Memory::GetPointer(macPtr);

		// MAC address is always 6 pairs of hex digits.
		// TODO: Funny stuff happens if it's too short.
		u8 value = 0;
		for (int i = 0; i < 6 && *buffer != 0; ++i) {
			value = 0;

			int c = hex_to_digit(*buffer++);
			if (c != -1) {
				value |= c << 4;
			}
			c = hex_to_digit(*buffer++);
			if (c != -1) {
				value |= c;
			}

			*mac++ = value;

			// Skip a single character in between.
			// TODO: Strange behavior on the PSP, let's just null check.
			if (*buffer++ == 0) {
				break;
			}
		}

		// Seems to maybe kinda return the last value.  Probably returns void.
		return value;
	} else {
		// Possibly a void function, seems to return this on bad args (or crash.)
		return 0;
	}
}


// Write static data since we don't actually manage any memory for sceNet* yet.
int sceNetGetMallocStat(u32 statPtr) {
	WARN_LOG(SCENET, "UNTESTED sceNetGetMallocStat(%x)", statPtr);
	if(Memory::IsValidAddress(statPtr))
		Memory::WriteStruct(statPtr, &netMallocStat);
	else
		ERROR_LOG(SCENET, "UNTESTED sceNetGetMallocStat(%x): tried to request invalid address!", statPtr);

	return 0;
}

int sceNetInetInit() {
	ERROR_LOG(SCENET, "UNIMPL sceNetInetInit()");
	if (netInetInited)
		return ERROR_NET_INET_ALREADY_INITIALIZED;
	netInetInited = true;

	return 0;
}

int sceNetInetTerm() {
	ERROR_LOG(SCENET, "UNIMPL sceNetInetTerm()");
	netInetInited = false;

	return 0;
}

int sceNetApctlInit() {
	ERROR_LOG(SCENET, "UNIMPL sceNetApctlInit()");
	if (netApctlInited)
		return ERROR_NET_APCTL_ALREADY_INITIALIZED;
	netApctlInited = true;

	return 0;
}

int sceNetApctlTerm() {
	ERROR_LOG(SCENET, "UNIMPL sceNeApctlTerm()");
	netApctlInited = false;
	
	return 0;
}

// TODO: How many handlers can the PSP actually have for Apctl?
// TODO: Should we allow the same handler to be added more than once?
u32 sceNetApctlAddHandler(u32 handlerPtr, u32 handlerArg) {
	bool foundHandler = false;
	u32 retval = 0;
	struct ApctlHandler handler;
	memset(&handler, 0, sizeof(handler));

	while (apctlHandlers.find(retval) != apctlHandlers.end())
		++retval;

	handler.entryPoint = handlerPtr;
	handler.argument = handlerArg;

	for(std::map<int, ApctlHandler>::iterator it = apctlHandlers.begin(); it != apctlHandlers.end(); it++) {
		if(it->second.entryPoint == handlerPtr) {
			foundHandler = true;
			break;
		}
	}

	if(!foundHandler && Memory::IsValidAddress(handlerPtr)) {
		if(apctlHandlers.size() >= MAX_APCTL_HANDLERS) {
			ERROR_LOG(SCENET, "UNTESTED sceNetApctlAddHandler(%x, %x): Too many handlers", handlerPtr, handlerArg);
			retval = ERROR_NET_ADHOCCTL_TOO_MANY_HANDLERS; // TODO: What's the proper error code for Apctl's TOO_MANY_HANDLERS?
			return retval;
		}
		apctlHandlers[retval] = handler;
		WARN_LOG(SCENET, "UNTESTED sceNetApctlAddHandler(%x, %x): added handler %d", handlerPtr, handlerArg, retval);
	}
	else
		ERROR_LOG(SCENET, "UNTESTED sceNetApctlAddHandler(%x, %x): Same handler already exists", handlerPtr, handlerArg);


	// The id to return is the number of handlers currently registered
	return retval;
}

int sceNetApctlDelHandler(u32 handlerID) {
	if(apctlHandlers.find(handlerID) != apctlHandlers.end()) {
		apctlHandlers.erase(handlerID);
		WARN_LOG(SCENET, "UNTESTED sceNetapctlDelHandler(%d): deleted handler %d", handlerID, handlerID);
	}
	else
		ERROR_LOG(SCENET, "UNTESTED sceNetapctlDelHandler(%d): asked to delete invalid handler %d", handlerID, handlerID);

	return 0;
}

int sceNetInetInetAton(const char *hostname, u32 addrPtr) {
	ERROR_LOG(SCENET, "UNIMPL sceNetInetInetAton(%s, %08x)", hostname, addrPtr);
	return -1;
}

int sceNetInetRecv(int socket, u32 bufPtr, u32 bufLen, u32 flags) {
	ERROR_LOG(SCENET, "UNIMPL sceNetInetRecv(%i, %08x, %i, %08x)", socket, bufPtr, bufLen, flags);
	return -1;
}

int sceNetInetSend(int socket, u32 bufPtr, u32 bufLen, u32 flags) {
	ERROR_LOG(SCENET, "UNIMPL sceNetInetSend(%i, %08x, %i, %08x)", socket, bufPtr, bufLen, flags);
	return -1;
}

int sceNetInetGetErrno() {
	ERROR_LOG(SCENET, "UNIMPL sceNetInetGetErrno()");
	return -1;
}

int sceNetInetSocket(int domain, int type, int protocol) {
	ERROR_LOG(SCENET, "UNIMPL sceNetInetSocket(%i, %i, %i)", domain, type, protocol);
	return -1;
}

int sceNetInetSetsockopt(int socket, int level, int optname, u32 optvalPtr, int optlen) {
	ERROR_LOG(SCENET, "UNIMPL sceNetInetSetsockopt(%i, %i, %i, %08x, %i)", socket, level, optname, optvalPtr, optlen);
	return -1;
}

int sceNetInetConnect(int socket, u32 sockAddrInternetPtr, int addressLength) {
	ERROR_LOG(SCENET, "UNIMPL sceNetInetConnect(%i, %08x, %i)", socket, sockAddrInternetPtr, addressLength);
	return -1;
}

int sceNetApctlDisconnect() {
	ERROR_LOG(SCENET, "UNIMPL %s()", __FUNCTION__);
	// Like its 'sister' function sceNetAdhocctlDisconnect, we need to alert Apctl handlers that a disconnect took place
	// or else games like Phantasy Star Portable 2 will hang at certain points (e.g. returning to the main menu after trying to connect to PSN).
	__UpdateApctlHandlers(0, 0, PSP_NET_APCTL_EVENT_DISCONNECT_REQUEST, 0);
	return 0;
}

const HLEFunction sceNet[] = {
	{0x39AF39A6, WrapU_UUUUU<sceNetInit>, "sceNetInit"},
	{0x281928A9, WrapU_V<sceNetTerm>, "sceNetTerm"},
	{0x89360950, WrapI_UU<sceNetEtherNtostr>, "sceNetEtherNtostr"},
	{0xd27961c9, WrapI_UU<sceNetEtherStrton>, "sceNetEtherStrton"},
	{0x0bf0a3ae, WrapU_U<sceNetGetLocalEtherAddr>, "sceNetGetLocalEtherAddr"},
	{0x50647530, 0, "sceNetFreeThreadinfo"},
	{0xcc393e48, WrapI_U<sceNetGetMallocStat>, "sceNetGetMallocStat"},
	{0xad6844c6, 0, "sceNetThreadAbort"},
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
	{0x4cfe4e56, 0, "sceNetInetShutdown"},
	{0xa9ed66b9, WrapI_V<sceNetInetTerm>, "sceNetInetTerm"},
	{0x8b7b220f, WrapI_III<sceNetInetSocket>, "sceNetInetSocket"},
	{0x2fe71fe7, WrapI_IIIUI<sceNetInetSetsockopt>, "sceNetInetSetsockopt"},
	{0x4a114c7c, 0, "sceNetInetGetsockopt"}, 
	{0x410b34aa, WrapI_IUI<sceNetInetConnect>, "sceNetInetConnect"},
	{0x805502DD, 0, "sceNetInetCloseWithRST"},
	{0xd10a1a7a, 0, "sceNetInetListen"},
	{0xdb094e1b, 0, "sceNetInetAccept"},
	{0xfaabb1dd, 0, "sceNetInetPoll"},
	{0x5be8d595, 0, "sceNetInetSelect"},
	{0x8d7284ea, 0, "sceNetInetClose"},
	{0xcda85c99, WrapI_IUUU<sceNetInetRecv>, "sceNetInetRecv"},
	{0xc91142e4, 0, "sceNetInetRecvfrom"},
	{0xeece61d2, 0, "sceNetInetRecvmsg"},
	{0x7aa671bc, WrapI_IUUU<sceNetInetSend>, "sceNetInetSend"},
	{0x05038fc7, 0, "sceNetInetSendto"},
	{0x774e36f4, 0, "sceNetInetSendmsg"},
	{0xfbabe411, WrapI_V<sceNetInetGetErrno>, "sceNetInetGetErrno"},
	{0x1a33f9ae, 0, "sceNetInetBind"},
	{0xb75d5b0a, 0, "sceNetInetInetAddr"},
	{0x1BDF5D13, WrapI_CU<sceNetInetInetAton>, "sceNetInetInetAton"},
	{0xd0792666, 0, "sceNetInetInetNtop"},
	{0xE30B8C19, 0, "sceNetInetInetPton"},
	{0x8ca3a97e, 0, "sceNetInetGetPspError"},
	{0xE247B6D6, 0, "sceNetInetGetpeername"},
	{0x162e6fd5, 0, "sceNetInetGetsockname"},
	{0x80A21ABD, 0, "sceNetInetSocketAbort"},
	{0x39b0c7d3, 0, "sceNetInetGetUdpcbstat"},
	{0xb3888ad4, 0, "sceNetInetGetTcpcbstat"},
};

const HLEFunction sceNetApctl[] = {
	{0xCFB957C6, 0, "sceNetApctlConnect"},
	{0x24fe91a1, &WrapI_V<sceNetApctlDisconnect>, "sceNetApctlDisconnect" },
	{0x5deac81b, 0, "sceNetApctlGetState"},
	{0x8abadd51, WrapU_UU<sceNetApctlAddHandler>, "sceNetApctlAddHandler"},
	{0xe2f91f9b, WrapI_V<sceNetApctlInit>, "sceNetApctlInit"},
	{0x5963991b, WrapI_U<sceNetApctlDelHandler>, "sceNetApctlDelHandler"},
	{0xb3edd0ec, WrapI_V<sceNetApctlTerm>, "sceNetApctlTerm"},
	{0x2BEFDF23, 0, "sceNetApctlGetInfo"},
	{0xa3e77e13, 0, "sceNetApctlScanSSID2"},
	{0xe9b2e5e6, 0, "sceNetApctlScanUser"},
	{0xf25a5006, 0, "sceNetApctlGetBSSDescIDList2"},
	{0x2935c45b, 0, "sceNetApctlGetBSSDescEntry2"},
	{0x04776994, 0, "sceNetApctlGetBSSDescEntryUser"},
	{0x6bddcb8c, 0, "sceNetApctlGetBSSDescIDListUser"},
};

const HLEFunction sceWlanDrv[] = {
	{0xd7763699, WrapU_V<sceWlanGetSwitchState>, "sceWlanGetSwitchState"},
	{0x0c622081, WrapU_U<sceWlanGetEtherAddr>, "sceWlanGetEtherAddr"},
	{0x93440B11, WrapU_V<sceWlanDevIsPowerOn>, "sceWlanDevIsPowerOn"},
};

void Register_sceNet() {
	RegisterModule("sceNet", ARRAY_SIZE(sceNet), sceNet);
	RegisterModule("sceNetResolver", ARRAY_SIZE(sceNetResolver), sceNetResolver);
	RegisterModule("sceNetInet", ARRAY_SIZE(sceNetInet), sceNetInet);
	RegisterModule("sceNetApctl", ARRAY_SIZE(sceNetApctl), sceNetApctl);
}

void Register_sceWlanDrv() {
	RegisterModule("sceWlanDrv", ARRAY_SIZE(sceWlanDrv), sceWlanDrv);
}
