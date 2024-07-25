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

#if __linux__ || __APPLE__ || defined(__OpenBSD__)
#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <fcntl.h>
#endif

#include "Common/Data/Text/Parsers.h"

#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Common/Serialize/SerializeMap.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/HLE/sceKernelMemory.h"
#include "Core/MIPS/MIPS.h"
#include "Core/Config.h"
#include "Core/MemMapHelpers.h"
#include "Core/Reporting.h"
#include "Core/Util/PortManager.h"

#include "sceKernel.h"
#include "sceKernelThread.h"
#include "sceUtility.h"

#include "Core/HLE/proAdhoc.h"
#include "Core/HLE/sceNetAdhoc.h"
#include "Core/HLE/sceNet.h"
#include "Core/HLE/sceNp.h"
#include "Core/CoreTiming.h"
#include "Core/Instance.h"

#if PPSSPP_PLATFORM(SWITCH) && !defined(INADDR_NONE)
// Missing toolchain define
#define INADDR_NONE 0xFFFFFFFF
#endif

bool netInited;
bool netInetInited;

u32 netDropRate = 0;
u32 netDropDuration = 0;
u32 netPoolAddr = 0;
u32 netThread1Addr = 0;
u32 netThread2Addr = 0;

static struct SceNetMallocStat netMallocStat;

static std::map<int, ApctlHandler> apctlHandlers;

SceNetApctlInfoInternal netApctlInfo;

bool netApctlInited;
u32 netApctlState;
u32 apctlThreadHackAddr = 0;
u32_le apctlThreadCode[3];
SceUID apctlThreadID = 0;
int apctlStateEvent = -1;
int actionAfterApctlMipsCall;
std::recursive_mutex apctlEvtMtx;
std::deque<ApctlArgs> apctlEvents;

u32 Net_Term();
int NetApctl_Term();
void NetApctl_InitInfo();

void AfterApctlMipsCall::DoState(PointerWrap & p) {
	auto s = p.Section("AfterApctlMipsCall", 1, 1);
	if (!s)
		return;
	// Just in case there are "s" corruption in the future where s.ver is a negative number
	if (s >= 1) {
		Do(p, handlerID);
		Do(p, oldState);
		Do(p, newState);
		Do(p, event);
		Do(p, error);
		Do(p, argsAddr);
	} else {
		handlerID = -1;
		oldState = 0;
		newState = 0;
		event = 0;
		error = 0;
		argsAddr = 0;
	}
}

void AfterApctlMipsCall::run(MipsCall& call) {
	u32 v0 = currentMIPS->r[MIPS_REG_V0];
	DEBUG_LOG(Log::sceNet, "AfterApctlMipsCall::run [ID=%i][OldState=%d][NewState=%d][Event=%d][Error=%d][ArgsPtr=%08x] [cbId: %u][retV0: %08x]", handlerID, oldState, newState, event, error, argsAddr, call.cbId, v0);
	//call.setReturnValue(v0);
}

void AfterApctlMipsCall::SetData(int HandlerID, int OldState, int NewState, int Event, int Error, u32_le ArgsAddr) {
	handlerID = HandlerID;
	oldState = OldState;
	newState = NewState;
	event = Event;
	error = Error;
	argsAddr = ArgsAddr;
}

void InitLocalhostIP() {
	// The entire 127.*.*.* is reserved for loopback.
	uint32_t localIP = 0x7F000001 + PPSSPP_ID - 1;

	g_localhostIP.in.sin_family = AF_INET;
	g_localhostIP.in.sin_addr.s_addr = htonl(localIP);
	g_localhostIP.in.sin_port = 0;

	std::string serverStr = StripSpaces(g_Config.proAdhocServer);
	isLocalServer = (!strcasecmp(serverStr.c_str(), "localhost") || serverStr.find("127.") == 0);
}

static void __ApctlState(u64 userdata, int cyclesLate) {
	SceUID threadID = userdata >> 32;
	int uid = (int)(userdata & 0xFFFFFFFF);
	int event = uid - 1;

	s64 result = 0;
	u32 error = 0;

	SceUID waitID = __KernelGetWaitID(threadID, WAITTYPE_NET, error);
	if (waitID == 0 || error != 0) {
		WARN_LOG(Log::sceNet, "sceNetApctl State WaitID(%i) on Thread(%i) already woken up? (error: %08x)", uid, threadID, error);
		return;
	}

	u32 waitVal = __KernelGetWaitValue(threadID, error);
	if (error == 0) {
		netApctlState = waitVal;
	}

	__KernelResumeThreadFromWait(threadID, result);
	DEBUG_LOG(Log::sceNet, "Returning (WaitID: %d, error: %08x) Result (%08x) of sceNetApctl - Event: %d, State: %d", waitID, error, (int)result, event, netApctlState);
}

// Used to change Apctl State after a delay and before executing callback mipscall (since we don't have beforeAction)
int ScheduleApctlState(int event, int newState, int usec, const char* reason) {
	int uid = event + 1;

	u64 param = ((u64)__KernelGetCurThread()) << 32 | uid;
	CoreTiming::ScheduleEvent(usToCycles(usec), apctlStateEvent, param);
	__KernelWaitCurThread(WAITTYPE_NET, uid, newState, 0, false, reason);

	return 0;
}

void __NetApctlInit() {
	netApctlInited = false;
	netApctlState = PSP_NET_APCTL_STATE_DISCONNECTED;
	apctlStateEvent = CoreTiming::RegisterEvent("__ApctlState", __ApctlState);
	apctlHandlers.clear();
	apctlEvents.clear();
	memset(&netApctlInfo, 0, sizeof(netApctlInfo));
}

static void __ResetInitNetLib() {
	netInited = false;
	netInetInited = false;

	memset(&netMallocStat, 0, sizeof(netMallocStat));
	memset(&parameter, 0, sizeof(parameter));
}

void __NetCallbackInit() {
	// Init Network Callbacks
	dummyThreadHackAddr = __CreateHLELoop(dummyThreadCode, "sceNetAdhoc", "__NetTriggerCallbacks", "dummythreadhack");
	matchingThreadHackAddr = __CreateHLELoop(matchingThreadCode, "sceNetAdhocMatching", "__NetMatchingCallbacks", "matchingThreadHack");
	apctlThreadHackAddr = __CreateHLELoop(apctlThreadCode, "sceNetApctl", "__NetApctlCallbacks", "apctlThreadHack");

	// Newer one should be placed last to prevent callbacks going to the wrong after action after loading from old save state
	actionAfterMatchingMipsCall = __KernelRegisterActionType(AfterMatchingMipsCall::Create);
	actionAfterAdhocMipsCall = __KernelRegisterActionType(AfterAdhocMipsCall::Create);
	actionAfterApctlMipsCall = __KernelRegisterActionType(AfterApctlMipsCall::Create);
}

void __NetInit() {
	// Windows: Assuming WSAStartup already called beforehand
	portOffset = g_Config.iPortOffset;
	isOriPort = g_Config.bEnableUPnP && g_Config.bUPnPUseOriginalPort;
	minSocketTimeoutUS = g_Config.iMinTimeout * 1000UL;

	// Init Default AdhocServer struct
	g_adhocServerIP.in.sin_family = AF_INET;
	g_adhocServerIP.in.sin_port = htons(SERVER_PORT); //27312 // Maybe read this from config too
	g_adhocServerIP.in.sin_addr.s_addr = INADDR_NONE;

	dummyPeekBuf64k = (char*)malloc(dummyPeekBuf64kSize);
	InitLocalhostIP();

	SceNetEtherAddr mac;
	getLocalMac(&mac);
	INFO_LOG(Log::sceNet, "LocalHost IP will be %s [%s]", ip2str(g_localhostIP.in.sin_addr).c_str(), mac2str(&mac).c_str());
	
	// TODO: May be we should initialize & cleanup somewhere else than here for PortManager to be used as general purpose for whatever port forwarding PPSSPP needed
	__UPnPInit();

	__ResetInitNetLib();
	__NetApctlInit();
	__NetCallbackInit();
}

void __NetApctlShutdown() {
	if (apctlThreadHackAddr) {
		kernelMemory.Free(apctlThreadHackAddr);
		apctlThreadHackAddr = 0;
	}
	apctlHandlers.clear();
	apctlEvents.clear();
}

void __NetShutdown() {
	// Network Cleanup
	Net_Term();

	__NetApctlShutdown();
	__ResetInitNetLib();

	// Since PortManager supposed to be general purpose for whatever port forwarding PPSSPP needed, may be we shouldn't clear & restore ports in here? it will be cleared and restored by PortManager's destructor when exiting PPSSPP anyway
	__UPnPShutdown();

	free(dummyPeekBuf64k);
}

static void __UpdateApctlHandlers(u32 oldState, u32 newState, u32 flag, u32 error) {
	std::lock_guard<std::recursive_mutex> apctlGuard(apctlEvtMtx);
	apctlEvents.push_back({ oldState, newState, flag, error });
}

// Make sure MIPS calls have been fully executed before the next notifyApctlHandlers
void notifyApctlHandlers(int oldState, int newState, int flag, int error) {
	__UpdateApctlHandlers(oldState, newState, flag, error);
}

void netValidateLoopMemory() {
	// Allocate Memory if it wasn't valid/allocated after loaded from old SaveState
	if (!apctlThreadHackAddr || (apctlThreadHackAddr && strcmp("apctlThreadHack", kernelMemory.GetBlockTag(apctlThreadHackAddr)) != 0)) {
		u32 blockSize = sizeof(apctlThreadCode);
		apctlThreadHackAddr = kernelMemory.Alloc(blockSize, false, "apctlThreadHack");
		if (apctlThreadHackAddr) Memory::Memcpy(apctlThreadHackAddr, apctlThreadCode, sizeof(apctlThreadCode));
	}
}

// This feels like a dubious proposition, mostly...
void __NetDoState(PointerWrap &p) {
	auto s = p.Section("sceNet", 1, 5);
	if (!s)
		return;

	auto cur_netInited = netInited;
	auto cur_netInetInited = netInetInited;
	auto cur_netApctlInited = netApctlInited;

	Do(p, netInited);
	Do(p, netInetInited);
	Do(p, netApctlInited);
	Do(p, apctlHandlers);
	Do(p, netMallocStat);
	if (s < 2) {
		netDropRate = 0;
		netDropDuration = 0;
	} else {
		Do(p, netDropRate);
		Do(p, netDropDuration);
	}
	if (s < 3) {
		netPoolAddr = 0;
		netThread1Addr = 0;
		netThread2Addr = 0;
	} else {
		Do(p, netPoolAddr);
		Do(p, netThread1Addr);
		Do(p, netThread2Addr);
	}
	if (s >= 4) {
		Do(p, netApctlState);
		Do(p, netApctlInfo);
		Do(p, actionAfterApctlMipsCall);
		if (actionAfterApctlMipsCall != -1) {
			__KernelRestoreActionType(actionAfterApctlMipsCall, AfterApctlMipsCall::Create);
		}
		Do(p, apctlThreadHackAddr);
		Do(p, apctlThreadID);
	}
	else {
		actionAfterApctlMipsCall = -1;
		apctlThreadHackAddr = 0;
		apctlThreadID = 0;
	}
	if (s >= 5) {
		Do(p, apctlStateEvent);
	} else {
		apctlStateEvent = -1;
	}
	CoreTiming::RestoreRegisterEvent(apctlStateEvent, "__ApctlState", __ApctlState);

	if (p.mode == p.MODE_READ) {
		// Let's not change "Inited" value when Loading SaveState in the middle of multiplayer to prevent memory & port leaks
		netApctlInited = cur_netApctlInited;
		netInetInited = cur_netInetInited;
		netInited = cur_netInited;

		// Discard leftover events
		apctlEvents.clear();
	}
}

template <typename I> std::string num2hex(I w, size_t hex_len) {
	static const char* digits = "0123456789ABCDEF";
	std::string rc(hex_len, '0');
	for (size_t i = 0, j = (hex_len - 1) * 4; i < hex_len; ++i, j -= 4)
		rc[i] = digits[(w >> j) & 0x0f];
	return rc;
}

std::string error2str(u32 errorCode) {
	std::string str = "";
	if (((errorCode >> 31) & 1) != 0)
		str += "ERROR ";
	if (((errorCode >> 30) & 1) != 0)
		str += "CRITICAL ";
	switch ((errorCode >> 16) & 0xfff) {
	case 0x41:
		str += "NET ";
		break;
	default:
		str += "UNK"+num2hex(u16((errorCode >> 16) & 0xfff), 3)+" ";
	}
	switch ((errorCode >> 8) & 0xff) {
	case 0x00:
		str += "COMMON ";
		break;
	case 0x01:
		str += "CORE ";
		break;
	case 0x02:
		str += "INET ";
		break;
	case 0x03:
		str += "POECLIENT ";
		break;
	case 0x04:
		str += "RESOLVER ";
		break;
	case 0x05:
		str += "DHCP ";
		break;
	case 0x06:
		str += "ADHOC_AUTH ";
		break;
	case 0x07:
		str += "ADHOC ";
		break;
	case 0x08:
		str += "ADHOC_MATCHING ";
		break;
	case 0x09:
		str += "NETCNF ";
		break;
	case 0x0a:
		str += "APCTL ";
		break;
	case 0x0b:
		str += "ADHOCCTL ";
		break;
	case 0x0c:
		str += "UNKNOWN1 ";
		break;
	case 0x0d:
		str += "WLAN ";
		break;
	case 0x0e:
		str += "EAPOL ";
		break;
	case 0x0f:
		str += "8021x ";
		break;
	case 0x10:
		str += "WPA ";
		break;
	case 0x11:
		str += "UNKNOWN2 ";
		break;
	case 0x12:
		str += "TRANSFER ";
		break;
	case 0x13:
		str += "ADHOC_DISCOVER ";
		break;
	case 0x14:
		str += "ADHOC_DIALOG ";
		break;
	case 0x15:
		str += "WISPR ";
		break;
	default:
		str += "UNKNOWN"+num2hex(u8((errorCode >> 8) & 0xff))+" ";
	}
	str += num2hex(u8(errorCode & 0xff));
	return str;
}

void __NetApctlCallbacks()
{
	std::lock_guard<std::recursive_mutex> apctlGuard(apctlEvtMtx);
	hleSkipDeadbeef();
	int delayus = 10000;

	// We are temporarily borrowing APctl thread for NpAuth callbacks for testing to simulate authentication
	if (!npAuthEvents.empty())
	{
		auto args = npAuthEvents.front();
		auto& id = args.data[0];
		auto& result = args.data[1];
		auto& argAddr = args.data[2];
		npAuthEvents.pop_front();

		delayus = (adhocEventDelay + adhocExtraDelay);

		int handlerID = id - 1;
		for (std::map<int, NpAuthHandler>::iterator it = npAuthHandlers.begin(); it != npAuthHandlers.end(); ++it) {
			if (it->first == handlerID) {
				DEBUG_LOG(Log::sceNet, "NpAuthCallback [HandlerID=%i][RequestID=%d][Result=%d][ArgsPtr=%08x]", it->first, id, result, it->second.argument);
				// TODO: Update result / args.data[1] with the actual ticket length (or error code?)
				hleEnqueueCall(it->second.entryPoint, 3, args.data);
			}
		}
	}

	// How AP works probably like this: Game use sceNetApctl function -> sceNetApctl let the hardware know and do their's thing and have a new State -> Let the game know the resulting State through Event on their handler
	if (!apctlEvents.empty())
	{
		auto args = apctlEvents.front();
		auto& oldState = args.data[0];
		auto& newState = args.data[1];
		auto& event = args.data[2];
		auto& error = args.data[3];
		apctlEvents.pop_front();

		// Adjust delay according to current event.
		if (event == PSP_NET_APCTL_EVENT_CONNECT_REQUEST || event == PSP_NET_APCTL_EVENT_GET_IP || event == PSP_NET_APCTL_EVENT_SCAN_REQUEST)
			delayus = adhocEventDelay;
		else
			delayus = adhocEventPollDelay;

		// Do we need to change the oldState? even if there was error?
		//if (error == 0)
		//	oldState = netApctlState;

		// Need to make sure netApctlState is updated before calling the callback's mipscall so the game can GetState()/GetInfo() within their handler's subroutine and make use the new State/Info
		// Should we update NewState & Error accordingly to Event before executing the mipscall ? sceNetApctl* functions might want to set the error value tho, so we probably should leave it untouched, right?
		//error = 0;
		switch (event) {
		case PSP_NET_APCTL_EVENT_CONNECT_REQUEST:
			newState = PSP_NET_APCTL_STATE_JOINING; // Should we set the State to PSP_NET_APCTL_STATE_DISCONNECTED if there was error?
			if (error == 0) 
				apctlEvents.push_front({ newState, newState, PSP_NET_APCTL_EVENT_ESTABLISHED, 0 }); // Should we use PSP_NET_APCTL_EVENT_EAP_AUTH if securityType is not NONE?
			break;

		case PSP_NET_APCTL_EVENT_ESTABLISHED:
			newState = PSP_NET_APCTL_STATE_GETTING_IP;
			if (error == 0) 
				apctlEvents.push_front({ newState, newState, PSP_NET_APCTL_EVENT_GET_IP, 0 });
			break;

		case PSP_NET_APCTL_EVENT_GET_IP:
			newState = PSP_NET_APCTL_STATE_GOT_IP;
			NetApctl_InitInfo();
			break;

		case PSP_NET_APCTL_EVENT_DISCONNECT_REQUEST:
			newState = PSP_NET_APCTL_STATE_DISCONNECTED;
			break;

		case PSP_NET_APCTL_EVENT_SCAN_REQUEST:
			newState = PSP_NET_APCTL_STATE_SCANNING;
			if (error == 0) 
				apctlEvents.push_front({ newState, newState, PSP_NET_APCTL_EVENT_SCAN_COMPLETE, 0 });
			break;

		case PSP_NET_APCTL_EVENT_SCAN_COMPLETE:
			newState = PSP_NET_APCTL_STATE_DISCONNECTED;
			if (error == 0)
				apctlEvents.push_front({ newState, newState, PSP_NET_APCTL_EVENT_SCAN_STOP, 0 });
			break;

		case PSP_NET_APCTL_EVENT_SCAN_STOP:
			newState = PSP_NET_APCTL_STATE_DISCONNECTED;
			break;

		case PSP_NET_APCTL_EVENT_EAP_AUTH: // Is this suppose to happen between JOINING and ESTABLISHED ?
			newState = PSP_NET_APCTL_STATE_EAP_AUTH;
			if (error == 0) 
				apctlEvents.push_front({ newState, newState, PSP_NET_APCTL_EVENT_KEY_EXCHANGE, 0 }); // not sure if KEY_EXCHANGE is the next step after AUTH or not tho
			break;

		case PSP_NET_APCTL_EVENT_KEY_EXCHANGE: // Is this suppose to happen between JOINING and ESTABLISHED ?
			newState = PSP_NET_APCTL_STATE_KEY_EXCHANGE;
			if (error == 0) 
				apctlEvents.push_front({ newState, newState, PSP_NET_APCTL_EVENT_ESTABLISHED, 0 });
			break;

		case PSP_NET_APCTL_EVENT_RECONNECT:
			newState = PSP_NET_APCTL_STATE_DISCONNECTED;
			if (error == 0) 
				apctlEvents.push_front({ newState, newState, PSP_NET_APCTL_EVENT_CONNECT_REQUEST, 0 });
			break;
		}
		// Do we need to change the newState? even if there were error?
		//if (error != 0)
		//	newState = netApctlState;

		// Since 0 is a valid index to types_ we use -1 to detects if it was loaded from an old save state
		if (actionAfterApctlMipsCall < 0) {
			actionAfterApctlMipsCall = __KernelRegisterActionType(AfterApctlMipsCall::Create);
		}

		// Run mipscall. Should we skipped executing the mipscall if oldState == newState? 
		for (std::map<int, ApctlHandler>::iterator it = apctlHandlers.begin(); it != apctlHandlers.end(); ++it) {
			DEBUG_LOG(Log::sceNet, "ApctlCallback [ID=%i][OldState=%d][NewState=%d][Event=%d][Error=%08x][ArgsPtr=%08x]", it->first, oldState, newState, event, error, it->second.argument);
			args.data[4] = it->second.argument;
			AfterApctlMipsCall* after = (AfterApctlMipsCall*)__KernelCreateAction(actionAfterApctlMipsCall);
			after->SetData(it->first, oldState, newState, event, error, it->second.argument);
			hleEnqueueCall(it->second.entryPoint, 5, args.data, after);
		}
		// Similar to Adhocctl, new State might need to be set after delayed, right before executing the mipscall (ie. simulated beforeAction)
		ScheduleApctlState(event, newState, delayus, "apctl callback state");
		return;
	}

	// Must be delayed long enough whenever there is a pending callback to make sure previous callback & it's afterAction are fully executed
	sceKernelDelayThread(delayus);
}

static inline u32 AllocUser(u32 size, bool fromTop, const char *name) {
	u32 addr = userMemory.Alloc(size, fromTop, name);
	if (addr == -1)
		return 0;
	return addr;
}

static inline void FreeUser(u32 &addr) {
	if (addr != 0)
		userMemory.Free(addr);
	addr = 0;
}

u32 Net_Term() {
	// May also need to Terminate netAdhocctl and netAdhoc to free some resources & threads, since the game (ie. GTA:VCS, Wipeout Pulse, etc) might not called them before calling sceNetTerm and causing them to behave strangely on the next sceNetInit & sceNetAdhocInit
	NetAdhocctl_Term();
	NetAdhoc_Term();

	// TODO: Not implemented yet
	NetApctl_Term();
	//NetInet_Term();

	// Library is initialized
	if (netInited) {
		// Delete Adhoc Sockets
		deleteAllAdhocSockets();

		// Delete GameMode Buffer
		//deleteAllGMB();

		// Terminate Internet Library
		//sceNetInetTerm();

		// Unload Internet Modules (Just keep it in memory... unloading crashes?!)
		// if (_manage_modules != 0) sceUtilityUnloadModule(PSP_MODULE_NET_INET);
		// Library shutdown
	}

	FreeUser(netPoolAddr);
	FreeUser(netThread1Addr);
	FreeUser(netThread2Addr);
	netInited = false;

	return 0;
}

static u32 sceNetTerm() {
	WARN_LOG(Log::sceNet, "sceNetTerm() at %08x", currentMIPS->pc);
	int retval = Net_Term();

	// Give time to make sure everything are cleaned up
	hleEatMicro(adhocDefaultDelay);
	return retval;
}

/*
Parameters:
	poolsize	- Memory pool size (appears to be for the whole of the networking library).
	calloutprio	- Priority of the SceNetCallout thread.
	calloutstack	- Stack size of the SceNetCallout thread (defaults to 4096 on non 1.5 firmware regardless of what value is passed).
	netintrprio	- Priority of the SceNetNetintr thread.
	netintrstack	- Stack size of the SceNetNetintr thread (defaults to 4096 on non 1.5 firmware regardless of what value is passed).
*/
static int sceNetInit(u32 poolSize, u32 calloutPri, u32 calloutStack, u32 netinitPri, u32 netinitStack)  {
	// TODO: Create Network Threads using given priority & stack
	// TODO: The correct behavior is actually to allocate more and leak the other threads/pool.
	// But we reset here for historic reasons (GTA:VCS potentially triggers this.)
	if (netInited)
		Net_Term(); // This cleanup attempt might not worked when SaveState were loaded in the middle of multiplayer game and re-entering multiplayer, thus causing memory leaks & wasting binded ports. May be we shouldn't save/load "Inited" vars on SaveState?

	if (poolSize == 0) {
		return hleLogError(Log::sceNet, SCE_KERNEL_ERROR_ILLEGAL_MEMSIZE, "invalid pool size");
	} else if (calloutPri < 0x08 || calloutPri > 0x77) {
		return hleLogError(Log::sceNet, SCE_KERNEL_ERROR_ILLEGAL_PRIORITY, "invalid callout thread priority");
	} else if (netinitPri < 0x08 || netinitPri > 0x77) {
		return hleLogError(Log::sceNet, SCE_KERNEL_ERROR_ILLEGAL_PRIORITY, "invalid init thread priority");
	}

	// TODO: Should also start the threads, probably?  For now, let's just allocate.
	// TODO: Respect the stack size if firmware set to 1.50?
	u32 stackSize = 4096;
	netThread1Addr = AllocUser(stackSize, true, "netstack1");
	if (netThread1Addr == 0) {
		return hleLogError(Log::sceNet, SCE_KERNEL_ERROR_NO_MEMORY, "unable to allocate thread");
	}
	netThread2Addr = AllocUser(stackSize, true, "netstack2");
	if (netThread2Addr == 0) {
		FreeUser(netThread1Addr);
		return hleLogError(Log::sceNet, SCE_KERNEL_ERROR_NO_MEMORY, "unable to allocate thread");
	}

	netPoolAddr = AllocUser(poolSize, false, "netpool");
	if (netPoolAddr == 0) {
		FreeUser(netThread1Addr);
		FreeUser(netThread2Addr);
		return hleLogError(Log::sceNet, SCE_KERNEL_ERROR_NO_MEMORY, "unable to allocate pool");
	}

	WARN_LOG(Log::sceNet, "sceNetInit(poolsize=%d, calloutpri=%i, calloutstack=%d, netintrpri=%i, netintrstack=%d) at %08x", poolSize, calloutPri, calloutStack, netinitPri, netinitStack, currentMIPS->pc);
	
	netMallocStat.pool = poolSize - 0x20; // On Vantage Master Portable this is slightly (32 bytes) smaller than the poolSize arg when tested with JPCSP + prx files
	netMallocStat.maximum = 0x4050; // Dummy maximum foot print
	netMallocStat.free = netMallocStat.pool; // Dummy free size, we should set this high enough to prevent any issue (ie. Vantage Master Portable), this is probably the only field being checked by games?

	// Clear Socket Translator Memory
	memset(&adhocSockets, 0, sizeof(adhocSockets));

	netInited = true;
	return hleLogSuccessI(Log::sceNet, 0);
}

// Free(delete) thread info / data. 
// Normal usage: sceKernelDeleteThread followed by sceNetFreeThreadInfo with the same threadID as argument
static int sceNetFreeThreadinfo(SceUID thid) {
	ERROR_LOG(Log::sceNet, "UNIMPL sceNetFreeThreadinfo(%i)", thid);

	return 0;
}

// Abort a thread.
static int sceNetThreadAbort(SceUID thid) {
	ERROR_LOG(Log::sceNet, "UNIMPL sceNetThreadAbort(%i)", thid);

	return 0;
}

static u32 sceWlanGetEtherAddr(u32 addrAddr) {
	if (!Memory::IsValidRange(addrAddr, 6)) {
		// More correctly, it should crash.
		return hleLogError(Log::sceNet, SCE_KERNEL_ERROR_ILLEGAL_ADDR, "illegal address");
	}

	u8 *addr = Memory::GetPointerWriteUnchecked(addrAddr);
	if (PPSSPP_ID > 1) {
		Memory::Memset(addrAddr, PPSSPP_ID, 6);
		// Making sure the 1st 2-bits on the 1st byte of OUI are zero to prevent issue with some games (ie. Gran Turismo)
		addr[0] &= 0xfc;
	} else {
		// Read MAC Address from config
		if (!ParseMacAddress(g_Config.sMACAddress, addr)) {
			ERROR_LOG(Log::sceNet, "Error parsing mac address %s", g_Config.sMACAddress.c_str());
			Memory::Memset(addrAddr, 0, 6);
		}
	}
	NotifyMemInfo(MemBlockFlags::WRITE, addrAddr, 6, "WlanEtherAddr");

	return hleLogSuccessI(Log::sceNet, hleDelayResult(0, "get ether mac", 200));
}

static u32 sceNetGetLocalEtherAddr(u32 addrAddr) {
	// FIXME: Return 0x80410180 (pspnet[_core] error code?) before successful attempt to Create/Connect/Join a Group? (ie. adhocctlCurrentMode == ADHOCCTL_MODE_NONE)
	if (adhocctlCurrentMode == ADHOCCTL_MODE_NONE)
		return hleLogDebug(Log::sceNet, 0x80410180, "address not available?");

	return sceWlanGetEtherAddr(addrAddr);
}

static u32 sceWlanDevIsPowerOn() {
	return hleLogSuccessVerboseI(Log::sceNet, g_Config.bEnableWlan ? 1 : 0);
}

static u32 sceWlanGetSwitchState() {
	return hleLogSuccessVerboseI(Log::sceNet, g_Config.bEnableWlan ? 1 : 0);
}

// Probably a void function, but often returns a useful value.
static void sceNetEtherNtostr(u32 macPtr, u32 bufferPtr) {
	DEBUG_LOG(Log::sceNet, "sceNetEtherNtostr(%08x, %08x) at %08x", macPtr, bufferPtr, currentMIPS->pc);

	if (Memory::IsValidAddress(bufferPtr) && Memory::IsValidAddress(macPtr)) {
		char *buffer = (char *)Memory::GetPointerWriteUnchecked(bufferPtr);
		const u8 *mac = Memory::GetPointerUnchecked(macPtr);

		// MAC address is always 6 bytes / 48 bits.
		sprintf(buffer, "%02x:%02x:%02x:%02x:%02x:%02x",
			mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

		VERBOSE_LOG(Log::sceNet, "sceNetEtherNtostr - [%s]", buffer);
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
static void sceNetEtherStrton(u32 bufferPtr, u32 macPtr) {
	DEBUG_LOG(Log::sceNet, "sceNetEtherStrton(%08x, %08x)", bufferPtr, macPtr);

	if (Memory::IsValidAddress(bufferPtr) && Memory::IsValidAddress(macPtr)) {
		const char *buffer = (const char *)Memory::GetPointerUnchecked(bufferPtr);
		u8 *mac = Memory::GetPointerWrite(macPtr);

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

		VERBOSE_LOG(Log::sceNet, "sceNetEtherStrton - [%s]", mac2str((SceNetEtherAddr*)Memory::GetPointer(macPtr)).c_str());
		// Seems to maybe kinda return the last value.  Probably returns void.
		//return value;
	}
}


// Write static data since we don't actually manage any memory for sceNet* yet.
static int sceNetGetMallocStat(u32 statPtr) {
	VERBOSE_LOG(Log::sceNet, "UNTESTED sceNetGetMallocStat(%x) at %08x", statPtr, currentMIPS->pc);
	auto stat = PSPPointer<SceNetMallocStat>::Create(statPtr);
	if (!stat.IsValid())
		return hleLogError(Log::sceNet, 0, "invalid address");

	*stat = netMallocStat;
	stat.NotifyWrite("sceNetGetMallocStat");
	return 0;
}

static int sceNetInetInit() {
	ERROR_LOG(Log::sceNet, "UNIMPL sceNetInetInit()");
	if (netInetInited) return ERROR_NET_INET_ALREADY_INITIALIZED;
	netInetInited = true;

	return 0;
}

int sceNetInetTerm() {
	ERROR_LOG(Log::sceNet, "UNIMPL sceNetInetTerm()");
	netInetInited = false;

	return 0;
}

void NetApctl_InitInfo() {
	memset(&netApctlInfo, 0, sizeof(netApctlInfo));
	// Set dummy/fake values, these probably not suppose to have valid info before connected to an AP, right?
	std::string APname = "Wifi"; // fake AP/hotspot
	truncate_cpy(netApctlInfo.name, sizeof(netApctlInfo.name), APname.c_str());
	truncate_cpy(netApctlInfo.ssid, sizeof(netApctlInfo.ssid), APname.c_str());
	memcpy(netApctlInfo.bssid, "\1\1\2\2\3\3", sizeof(netApctlInfo.bssid)); // fake AP's mac address
	netApctlInfo.ssidLength = static_cast<unsigned int>(APname.length());
	netApctlInfo.strength = 99;
	netApctlInfo.channel = g_Config.iWlanAdhocChannel;
	if (netApctlInfo.channel == PSP_SYSTEMPARAM_ADHOC_CHANNEL_AUTOMATIC) netApctlInfo.channel = defaultWlanChannel;
	// Get Local IP Address
	sockaddr_in sockAddr;
	getLocalIp(&sockAddr); // This will be valid IP, we probably not suppose to have a valid IP before connected to any AP, right?
	char ipstr[INET_ADDRSTRLEN] = "127.0.0.1"; // Patapon 3 seems to try to get current IP using ApctlGetInfo() right after ApctlInit(), what kind of IP should we use as default before ApctlConnect()? it shouldn't be a valid IP, right?
	inet_ntop(AF_INET, &sockAddr.sin_addr, ipstr, sizeof(ipstr));
	truncate_cpy(netApctlInfo.ip, sizeof(netApctlInfo.ip), ipstr);
	// Change the last number to 1 to indicate a common dns server/internet gateway
	((u8*)&sockAddr.sin_addr.s_addr)[3] = 1;
	inet_ntop(AF_INET, &sockAddr.sin_addr, ipstr, sizeof(ipstr));
	truncate_cpy(netApctlInfo.gateway, sizeof(netApctlInfo.gateway), ipstr);
	truncate_cpy(netApctlInfo.primaryDns, sizeof(netApctlInfo.primaryDns), ipstr);
	truncate_cpy(netApctlInfo.secondaryDns, sizeof(netApctlInfo.secondaryDns), "8.8.8.8");
	truncate_cpy(netApctlInfo.subNetMask, sizeof(netApctlInfo.subNetMask), "255.255.255.0");
}

static int sceNetApctlInit(int stackSize, int initPriority) {
	WARN_LOG(Log::sceNet, "UNTESTED %s(%i, %i)", __FUNCTION__, stackSize, initPriority);
	if (netApctlInited)
		return ERROR_NET_APCTL_ALREADY_INITIALIZED;

	apctlEvents.clear();
	netApctlState = PSP_NET_APCTL_STATE_DISCONNECTED;

	// Set default value before connected to an AP
	memset(&netApctlInfo, 0, sizeof(netApctlInfo)); // NetApctl_InitInfo();
	std::string APname = "Wifi"; // fake AP/hotspot
	truncate_cpy(netApctlInfo.name, sizeof(netApctlInfo.name), APname.c_str());
	truncate_cpy(netApctlInfo.ssid, sizeof(netApctlInfo.ssid), APname.c_str());
	memcpy(netApctlInfo.bssid, "\1\1\2\2\3\3", sizeof(netApctlInfo.bssid)); // fake AP's mac address
	netApctlInfo.ssidLength = static_cast<unsigned int>(APname.length());
	truncate_cpy(netApctlInfo.ip, sizeof(netApctlInfo.ip), "0.0.0.0");
	truncate_cpy(netApctlInfo.gateway, sizeof(netApctlInfo.gateway), "0.0.0.0");
	truncate_cpy(netApctlInfo.primaryDns, sizeof(netApctlInfo.primaryDns), "0.0.0.0");
	truncate_cpy(netApctlInfo.secondaryDns, sizeof(netApctlInfo.secondaryDns), "0.0.0.0");
	truncate_cpy(netApctlInfo.subNetMask, sizeof(netApctlInfo.subNetMask), "0.0.0.0");

	// Create APctl fake-Thread
	netValidateLoopMemory();
	apctlThreadID = __KernelCreateThread("ApctlThread", __KernelGetCurThreadModuleId(), apctlThreadHackAddr, initPriority, stackSize, PSP_THREAD_ATTR_USER, 0, true);
	if (apctlThreadID > 0) {
		__KernelStartThread(apctlThreadID, 0, 0);
	}

	netApctlInited = true;

	return 0;
}

int NetApctl_Term() {
	// Cleanup Apctl resources
	// Delete fake PSP Thread
	if (apctlThreadID != 0) {
		__KernelStopThread(apctlThreadID, SCE_KERNEL_ERROR_THREAD_TERMINATED, "ApctlThread stopped");
		__KernelDeleteThread(apctlThreadID, SCE_KERNEL_ERROR_THREAD_TERMINATED, "ApctlThread deleted");
		apctlThreadID = 0;
	}

	netApctlInited = false;
	netApctlState = PSP_NET_APCTL_STATE_DISCONNECTED;

	return 0;
}

int sceNetApctlTerm() {
	WARN_LOG(Log::sceNet, "UNTESTED %s()", __FUNCTION__);
	return NetApctl_Term();
}

static int sceNetApctlGetInfo(int code, u32 pInfoAddr) {
	WARN_LOG(Log::sceNet, "UNTESTED %s(%i, %08x)", __FUNCTION__, code, pInfoAddr);

	if (!netApctlInited)
		return hleLogError(Log::sceNet, ERROR_NET_APCTL_NOT_IN_BSS, "apctl not in bss"); // Only have valid info after joining an AP and got an IP, right?

	switch (code) {
	case PSP_NET_APCTL_INFO_PROFILE_NAME:
		if (!Memory::IsValidRange(pInfoAddr, APCTL_PROFILENAME_MAXLEN))
			return hleLogError(Log::sceNet, -1, "apctl invalid arg");
		Memory::MemcpyUnchecked(pInfoAddr, netApctlInfo.name, APCTL_PROFILENAME_MAXLEN);
		NotifyMemInfo(MemBlockFlags::WRITE, pInfoAddr, APCTL_PROFILENAME_MAXLEN, "NetApctlGetInfo");
		DEBUG_LOG(Log::sceNet, "ApctlInfo - ProfileName: %s", netApctlInfo.name);
		break;
	case PSP_NET_APCTL_INFO_BSSID:
		if (!Memory::IsValidRange(pInfoAddr, ETHER_ADDR_LEN))
			return hleLogError(Log::sceNet, -1, "apctl invalid arg");
		Memory::MemcpyUnchecked(pInfoAddr, netApctlInfo.bssid, ETHER_ADDR_LEN);
		DEBUG_LOG(Log::sceNet, "ApctlInfo - BSSID: %s", mac2str((SceNetEtherAddr*)&netApctlInfo.bssid).c_str());
		NotifyMemInfo(MemBlockFlags::WRITE, pInfoAddr, ETHER_ADDR_LEN, "NetApctlGetInfo");
		break;
	case PSP_NET_APCTL_INFO_SSID:
		if (!Memory::IsValidRange(pInfoAddr, APCTL_SSID_MAXLEN))
			return hleLogError(Log::sceNet, -1, "apctl invalid arg");
		Memory::MemcpyUnchecked(pInfoAddr, netApctlInfo.ssid, APCTL_SSID_MAXLEN);
		NotifyMemInfo(MemBlockFlags::WRITE, pInfoAddr, APCTL_SSID_MAXLEN, "NetApctlGetInfo");
		DEBUG_LOG(Log::sceNet, "ApctlInfo - SSID: %s", netApctlInfo.ssid);
		break;
	case PSP_NET_APCTL_INFO_SSID_LENGTH:
		if (!Memory::IsValidRange(pInfoAddr, 4))
			return hleLogError(Log::sceNet, -1, "apctl invalid arg");
		Memory::WriteUnchecked_U32(netApctlInfo.ssidLength, pInfoAddr);
		NotifyMemInfo(MemBlockFlags::WRITE, pInfoAddr, 4, "NetApctlGetInfo");
		break;
	case PSP_NET_APCTL_INFO_SECURITY_TYPE:
		if (!Memory::IsValidRange(pInfoAddr, 4))
			return hleLogError(Log::sceNet, -1, "apctl invalid arg");
		Memory::WriteUnchecked_U32(netApctlInfo.securityType, pInfoAddr);
		NotifyMemInfo(MemBlockFlags::WRITE, pInfoAddr, 4, "NetApctlGetInfo");
		break;
	case PSP_NET_APCTL_INFO_STRENGTH:
		if (!Memory::IsValidRange(pInfoAddr, 1))
			return hleLogError(Log::sceNet, -1, "apctl invalid arg");
		Memory::WriteUnchecked_U8(netApctlInfo.strength, pInfoAddr);
		NotifyMemInfo(MemBlockFlags::WRITE, pInfoAddr, 1, "NetApctlGetInfo");
		break;
	case PSP_NET_APCTL_INFO_CHANNEL:
		if (!Memory::IsValidRange(pInfoAddr, 1))
			return hleLogError(Log::sceNet, -1, "apctl invalid arg");
		Memory::WriteUnchecked_U8(netApctlInfo.channel, pInfoAddr);
		NotifyMemInfo(MemBlockFlags::WRITE, pInfoAddr, 1, "NetApctlGetInfo");
		break;
	case PSP_NET_APCTL_INFO_POWER_SAVE:
		if (!Memory::IsValidRange(pInfoAddr, 1))
			return hleLogError(Log::sceNet, -1, "apctl invalid arg");
		Memory::WriteUnchecked_U8(netApctlInfo.powerSave, pInfoAddr);
		NotifyMemInfo(MemBlockFlags::WRITE, pInfoAddr, 1, "NetApctlGetInfo");
		break;
	case PSP_NET_APCTL_INFO_IP:
		if (!Memory::IsValidRange(pInfoAddr, APCTL_IPADDR_MAXLEN))
			return hleLogError(Log::sceNet, -1, "apctl invalid arg");
		Memory::MemcpyUnchecked(pInfoAddr, netApctlInfo.ip, APCTL_IPADDR_MAXLEN);
		NotifyMemInfo(MemBlockFlags::WRITE, pInfoAddr, APCTL_IPADDR_MAXLEN, "NetApctlGetInfo");
		DEBUG_LOG(Log::sceNet, "ApctlInfo - IP: %s", netApctlInfo.ip);
		break;
	case PSP_NET_APCTL_INFO_SUBNETMASK:
		if (!Memory::IsValidRange(pInfoAddr, APCTL_IPADDR_MAXLEN))
			return hleLogError(Log::sceNet, -1, "apctl invalid arg");
		Memory::MemcpyUnchecked(pInfoAddr, netApctlInfo.subNetMask, APCTL_IPADDR_MAXLEN);
		NotifyMemInfo(MemBlockFlags::WRITE, pInfoAddr, APCTL_IPADDR_MAXLEN, "NetApctlGetInfo");
		DEBUG_LOG(Log::sceNet, "ApctlInfo - SubNet Mask: %s", netApctlInfo.subNetMask);
		break;
	case PSP_NET_APCTL_INFO_GATEWAY:
		if (!Memory::IsValidRange(pInfoAddr, APCTL_IPADDR_MAXLEN))
			return hleLogError(Log::sceNet, -1, "apctl invalid arg");
		Memory::MemcpyUnchecked(pInfoAddr, netApctlInfo.gateway, APCTL_IPADDR_MAXLEN);
		DEBUG_LOG(Log::sceNet, "ApctlInfo - Gateway IP: %s", netApctlInfo.gateway);
		break;
	case PSP_NET_APCTL_INFO_PRIMDNS:
		if (!Memory::IsValidRange(pInfoAddr, APCTL_IPADDR_MAXLEN))
			return hleLogError(Log::sceNet, -1, "apctl invalid arg");
		Memory::MemcpyUnchecked(pInfoAddr, netApctlInfo.primaryDns, APCTL_IPADDR_MAXLEN);
		NotifyMemInfo(MemBlockFlags::WRITE, pInfoAddr, APCTL_IPADDR_MAXLEN, "NetApctlGetInfo");
		DEBUG_LOG(Log::sceNet, "ApctlInfo - Primary DNS: %s", netApctlInfo.primaryDns);
		break;
	case PSP_NET_APCTL_INFO_SECDNS:
		if (!Memory::IsValidRange(pInfoAddr, APCTL_IPADDR_MAXLEN))
			return hleLogError(Log::sceNet, -1, "apctl invalid arg");
		Memory::MemcpyUnchecked(pInfoAddr, netApctlInfo.secondaryDns, APCTL_IPADDR_MAXLEN);
		NotifyMemInfo(MemBlockFlags::WRITE, pInfoAddr, APCTL_IPADDR_MAXLEN, "NetApctlGetInfo");
		DEBUG_LOG(Log::sceNet, "ApctlInfo - Secondary DNS: %s", netApctlInfo.secondaryDns);
		break;
	case PSP_NET_APCTL_INFO_USE_PROXY:
		if (!Memory::IsValidRange(pInfoAddr, 4))
			return hleLogError(Log::sceNet, -1, "apctl invalid arg");
		Memory::WriteUnchecked_U32(netApctlInfo.useProxy, pInfoAddr);
		NotifyMemInfo(MemBlockFlags::WRITE, pInfoAddr, 4, "NetApctlGetInfo");
		break;
	case PSP_NET_APCTL_INFO_PROXY_URL:
		if (!Memory::IsValidRange(pInfoAddr, APCTL_URL_MAXLEN))
			return hleLogError(Log::sceNet, -1, "apctl invalid arg");
		Memory::MemcpyUnchecked(pInfoAddr, netApctlInfo.proxyUrl, APCTL_URL_MAXLEN);
		NotifyMemInfo(MemBlockFlags::WRITE, pInfoAddr, APCTL_URL_MAXLEN, "NetApctlGetInfo");
		DEBUG_LOG(Log::sceNet, "ApctlInfo - Proxy URL: %s", netApctlInfo.proxyUrl);
		break;
	case PSP_NET_APCTL_INFO_PROXY_PORT:
		if (!Memory::IsValidRange(pInfoAddr, 2))
			return hleLogError(Log::sceNet, -1, "apctl invalid arg");
		Memory::WriteUnchecked_U16(netApctlInfo.proxyPort, pInfoAddr);
		NotifyMemInfo(MemBlockFlags::WRITE, pInfoAddr, 2, "NetApctlGetInfo");
		break;
	case PSP_NET_APCTL_INFO_8021_EAP_TYPE:
		if (!Memory::IsValidRange(pInfoAddr, 4))
			return hleLogError(Log::sceNet, -1, "apctl invalid arg");
		Memory::WriteUnchecked_U32(netApctlInfo.eapType, pInfoAddr);
		NotifyMemInfo(MemBlockFlags::WRITE, pInfoAddr, 4, "NetApctlGetInfo");
		break;
	case PSP_NET_APCTL_INFO_START_BROWSER:
		if (!Memory::IsValidRange(pInfoAddr, 4))
			return hleLogError(Log::sceNet, -1, "apctl invalid arg");
		Memory::WriteUnchecked_U32(netApctlInfo.startBrowser, pInfoAddr);
		NotifyMemInfo(MemBlockFlags::WRITE, pInfoAddr, 4, "NetApctlGetInfo");
		break;
	case PSP_NET_APCTL_INFO_WIFISP:
		if (!Memory::IsValidRange(pInfoAddr, 4))
			return hleLogError(Log::sceNet, -1, "apctl invalid arg");
		Memory::WriteUnchecked_U32(netApctlInfo.wifisp, pInfoAddr);
		NotifyMemInfo(MemBlockFlags::WRITE, pInfoAddr, 4, "NetApctlGetInfo");
		break;
	default:
		return hleLogError(Log::sceNet, ERROR_NET_APCTL_INVALID_CODE, "apctl invalid code");
	}

	return hleLogSuccessI(Log::sceNet, 0);
}

int NetApctl_AddHandler(u32 handlerPtr, u32 handlerArg) {
	bool foundHandler = false;
	u32 retval = 0;
	struct ApctlHandler handler;
	memset(&handler, 0, sizeof(handler));

	while (apctlHandlers.find(retval) != apctlHandlers.end())
		++retval;

	handler.entryPoint = handlerPtr;
	handler.argument = handlerArg;

	for (std::map<int, ApctlHandler>::iterator it = apctlHandlers.begin(); it != apctlHandlers.end(); it++) {
		if (it->second.entryPoint == handlerPtr) {
			foundHandler = true;
			break;
		}
	}

	if (!foundHandler && Memory::IsValidAddress(handlerPtr)) {
		if (apctlHandlers.size() >= MAX_APCTL_HANDLERS) {
			ERROR_LOG(Log::sceNet, "Failed to Add handler(%x, %x): Too many handlers", handlerPtr, handlerArg);
			retval = ERROR_NET_ADHOCCTL_TOO_MANY_HANDLERS; // TODO: What's the proper error code for Apctl's TOO_MANY_HANDLERS?
			return retval;
		}
		apctlHandlers[retval] = handler;
		WARN_LOG(Log::sceNet, "Added Apctl handler(%x, %x): %d", handlerPtr, handlerArg, retval);
	}
	else {
		ERROR_LOG(Log::sceNet, "Existing Apctl handler(%x, %x)", handlerPtr, handlerArg);
	}

	// The id to return is the number of handlers currently registered
	return retval;
}

// TODO: How many handlers can the PSP actually have for Apctl?
// TODO: Should we allow the same handler to be added more than once?
static u32 sceNetApctlAddHandler(u32 handlerPtr, u32 handlerArg) {
	INFO_LOG(Log::sceNet, "%s(%08x, %08x)", __FUNCTION__, handlerPtr, handlerArg);
	return NetApctl_AddHandler(handlerPtr, handlerArg);
}

int NetApctl_DelHandler(u32 handlerID) {
	if (apctlHandlers.find(handlerID) != apctlHandlers.end()) {
		apctlHandlers.erase(handlerID);
		WARN_LOG(Log::sceNet, "Deleted Apctl handler: %d", handlerID);
	}
	else {
		ERROR_LOG(Log::sceNet, "Invalid Apctl handler: %d", handlerID);
	}
	return 0;
}

static int sceNetApctlDelHandler(u32 handlerID) {
	INFO_LOG(Log::sceNet, "%s(%d)", __FUNCTION__, handlerID);
	return NetApctl_DelHandler(handlerID);
}

static int sceNetInetInetAton(const char *hostname, u32 addrPtr) {
	ERROR_LOG(Log::sceNet, "UNIMPL sceNetInetInetAton(%s, %08x)", hostname, addrPtr);
	return -1;
}

int sceNetInetPoll(void *fds, u32 nfds, int timeout) { // timeout in miliseconds
	DEBUG_LOG(Log::sceNet, "UNTESTED sceNetInetPoll(%p, %d, %i) at %08x", fds, nfds, timeout, currentMIPS->pc);
	int retval = -1;
	SceNetInetPollfd *fdarray = (SceNetInetPollfd *)fds; // SceNetInetPollfd/pollfd, sceNetInetPoll() have similarity to BSD poll() but pollfd have different size on 64bit
//#ifdef _WIN32
	//WSAPoll only available for Vista or newer, so we'll use an alternative way for XP since Windows doesn't have poll function like *NIX
	if (nfds > FD_SETSIZE) return -1;
	fd_set readfds, writefds, exceptfds;
	FD_ZERO(&readfds); FD_ZERO(&writefds); FD_ZERO(&exceptfds);
	for (int i = 0; i < (s32)nfds; i++) {
		if (fdarray[i].events & (INET_POLLRDNORM)) FD_SET(fdarray[i].fd, &readfds); // (POLLRDNORM | POLLIN)
		if (fdarray[i].events & (INET_POLLWRNORM)) FD_SET(fdarray[i].fd, &writefds); // (POLLWRNORM | POLLOUT)
		//if (fdarray[i].events & (ADHOC_EV_ALERT)) // (POLLRDBAND | POLLPRI) // POLLERR 
		FD_SET(fdarray[i].fd, &exceptfds); 
		fdarray[i].revents = 0;
	}
	timeval tmout;
	tmout.tv_sec = timeout / 1000; // seconds
	tmout.tv_usec = (timeout % 1000) * 1000; // microseconds
	retval = select(nfds, &readfds, &writefds, &exceptfds, &tmout);
	if (retval < 0) return -1;
	retval = 0;
	for (int i = 0; i < (s32)nfds; i++) {
		if (FD_ISSET(fdarray[i].fd, &readfds)) fdarray[i].revents |= INET_POLLRDNORM; //POLLIN
		if (FD_ISSET(fdarray[i].fd, &writefds)) fdarray[i].revents |= INET_POLLWRNORM; //POLLOUT
		fdarray[i].revents &= fdarray[i].events;
		if (FD_ISSET(fdarray[i].fd, &exceptfds)) fdarray[i].revents |= ADHOC_EV_ALERT; // POLLPRI; // POLLERR; // can be raised on revents regardless of events bitmask?
		if (fdarray[i].revents) retval++;
	}
//#else
	/*
	// Doesn't work properly yet
	pollfd *fdtmp = (pollfd *)malloc(sizeof(pollfd) * nfds);
	// Note: sizeof(pollfd) = 16bytes in 64bit and 8bytes in 32bit, while sizeof(SceNetInetPollfd) is always 8bytes
	for (int i = 0; i < (s32)nfds; i++) {
		fdtmp[i].fd = fdarray[i].fd;
		fdtmp[i].events = 0;
		if (fdarray[i].events & INET_POLLRDNORM) fdtmp[i].events |= (POLLIN | POLLRDNORM | POLLRDBAND | POLLPRI);
		if (fdarray[i].events & INET_POLLWRNORM) fdtmp[i].events |= (POLLOUT | POLLWRNORM | POLLWRBAND);
		fdtmp[i].revents = 0;
		fdarray[i].revents = 0;
	}
	retval = poll(fdtmp, (nfds_t)nfds, timeout); //retval = WSAPoll(fdarray, nfds, timeout);
	for (int i = 0; i < (s32)nfds; i++) {
		if (fdtmp[i].revents & (POLLIN | POLLRDNORM | POLLRDBAND | POLLPRI)) fdarray[i].revents |= INET_POLLRDNORM;
		if (fdtmp[i].revents & (POLLOUT | POLLWRNORM | POLLWRBAND)) fdarray[i].revents |= INET_POLLWRNORM;
		fdarray[i].revents &= fdarray[i].events;
		if (fdtmp[i].revents & POLLERR) fdarray[i].revents |= POLLERR; //INET_POLLERR // can be raised on revents regardless of events bitmask?
	}
	free(fdtmp);
	*/
//#endif
	return retval;
}

static int sceNetInetRecv(int socket, u32 bufPtr, u32 bufLen, u32 flags) {
	ERROR_LOG(Log::sceNet, "UNIMPL sceNetInetRecv(%i, %08x, %i, %08x)", socket, bufPtr, bufLen, flags);
	return -1;
}

static int sceNetInetSend(int socket, u32 bufPtr, u32 bufLen, u32 flags) {
	ERROR_LOG(Log::sceNet, "UNIMPL sceNetInetSend(%i, %08x, %i, %08x)", socket, bufPtr, bufLen, flags);
	return -1;
}

static int sceNetInetGetErrno() {
	ERROR_LOG(Log::sceNet, "UNTESTED sceNetInetGetErrno()");
	int error = errno;
	switch (error) {
	case ETIMEDOUT:		
		return INET_ETIMEDOUT;
	case EISCONN:		
		return INET_EISCONN;
	case EINPROGRESS:	
		return INET_EINPROGRESS;
	//case EAGAIN:
	//	return INET_EAGAIN;
	}
	return error; //-1;
}

static int sceNetInetSocket(int domain, int type, int protocol) {
	ERROR_LOG(Log::sceNet, "UNIMPL sceNetInetSocket(%i, %i, %i)", domain, type, protocol);
	return -1;
}

static int sceNetInetSetsockopt(int socket, int level, int optname, u32 optvalPtr, int optlen) {
	ERROR_LOG(Log::sceNet, "UNIMPL sceNetInetSetsockopt(%i, %i, %i, %08x, %i)", socket, level, optname, optvalPtr, optlen);
	return -1;
}

static int sceNetInetConnect(int socket, u32 sockAddrInternetPtr, int addressLength) {
	ERROR_LOG(Log::sceNet, "UNIMPL sceNetInetConnect(%i, %08x, %i)", socket, sockAddrInternetPtr, addressLength);
	return -1;
}

int sceNetApctlConnect(int connIndex) {
	WARN_LOG(Log::sceNet, "UNTESTED %s(%i)", __FUNCTION__, connIndex);
	// Is this connIndex is the index to the scanning's result data or sceNetApctlGetBSSDescIDListUser result?
	__UpdateApctlHandlers(0, 0, PSP_NET_APCTL_EVENT_CONNECT_REQUEST, 0);
	//hleDelayResult(0, "give time to init/cleanup", adhocEventDelayMS * 1000);
	return 0;
}

static int sceNetApctlDisconnect() {
	ERROR_LOG(Log::sceNet, "UNIMPL %s()", __FUNCTION__);
	// Like its 'sister' function sceNetAdhocctlDisconnect, we need to alert Apctl handlers that a disconnect took place
	// or else games like Phantasy Star Portable 2 will hang at certain points (e.g. returning to the main menu after trying to connect to PSN).

	__UpdateApctlHandlers(0, 0, PSP_NET_APCTL_EVENT_DISCONNECT_REQUEST, 0);
	return 0;
}

int NetApctl_GetState() {
	return netApctlState;
}

static int sceNetApctlGetState(u32 pStateAddr) {
	//if (!netApctlInited) return hleLogError(Log::sceNet, ERROR_NET_APCTL_NOT_IN_BSS, "apctl not in bss");

	// Valid Arguments
	if (Memory::IsValidAddress(pStateAddr)) {
		// Return Thread Status
		Memory::Write_U32(NetApctl_GetState(), pStateAddr);
		// Return Success
		return hleLogSuccessI(Log::sceNet, 0);
	}

	return hleLogError(Log::sceNet, -1, "apctl invalid arg"); // 0x8002013A or ERROR_NET_WLAN_INVALID_ARG ?
}

int NetApctl_ScanUser() {
	// Scan probably only works when not in connected state, right?
	if (netApctlState != PSP_NET_APCTL_STATE_DISCONNECTED)
		return hleLogError(Log::sceNet, ERROR_NET_APCTL_NOT_DISCONNECTED, "apctl not disconnected");

	__UpdateApctlHandlers(0, 0, PSP_NET_APCTL_EVENT_SCAN_REQUEST, 0);
	return 0;
}

static int sceNetApctlScanUser() {
	ERROR_LOG(Log::sceNet, "UNIMPL %s()", __FUNCTION__);
	return NetApctl_ScanUser();
}

int NetApctl_GetBSSDescIDListUser(u32 sizeAddr, u32 bufAddr) {
	const int userInfoSize = 8; // 8 bytes per entry (next address + entry id)
	// Faking 4 entries, games like MGS:PW Recruit will need to have a different AP for each entry
	int entries = 4;
	if (!Memory::IsValidAddress(sizeAddr) || !Memory::IsValidAddress(bufAddr))
		return hleLogError(Log::sceNet, -1, "apctl invalid arg"); // 0x8002013A or ERROR_NET_WLAN_INVALID_ARG ?

	int size = Memory::Read_U32(sizeAddr);
	// Return size required
	Memory::Write_U32(entries * userInfoSize, sizeAddr);

	if (bufAddr != 0 && Memory::IsValidAddress(sizeAddr)) {
		int offset = 0;
		for (int i = 0; i < entries; i++) {
			// Check if enough space available to write the next structure
			if (offset + userInfoSize > size) {
				break;
			}

			DEBUG_LOG(Log::sceNet, "%s writing ID#%d to %08x", __FUNCTION__, i, bufAddr + offset);

			// Pointer to next Network structure in list
			Memory::Write_U32((i + 1) * userInfoSize + bufAddr, bufAddr + offset);
			offset += 4;

			// Entry ID
			Memory::Write_U32(i, bufAddr + offset);
			offset += 4;
		}
		// Fix the last Pointer
		if (offset > 0)
			Memory::Write_U32(0, bufAddr + offset - userInfoSize);
	}

	return 0;
}

static int sceNetApctlGetBSSDescIDListUser(u32 sizeAddr, u32 bufAddr) {
	WARN_LOG(Log::sceNet, "UNTESTED %s(%08x, %08x)", __FUNCTION__, sizeAddr, bufAddr);
	return NetApctl_GetBSSDescIDListUser(sizeAddr, bufAddr);
}

int NetApctl_GetBSSDescEntryUser(int entryId, int infoId, u32 resultAddr) {
	if (!Memory::IsValidAddress(resultAddr))
		return hleLogError(Log::sceNet, -1, "apctl invalid arg"); // 0x8002013A or ERROR_NET_WLAN_INVALID_ARG ?

	// Generate an SSID name
	char dummySSID[APCTL_SSID_MAXLEN] = "WifiAP0";
	dummySSID[6] += static_cast<char>(entryId);

	switch (infoId) {
	case PSP_NET_APCTL_DESC_IBSS: // IBSS, 6 bytes
		if (entryId == 0)
			Memory::Memcpy(resultAddr, netApctlInfo.bssid, sizeof(netApctlInfo.bssid), "GetBSSDescEntryUser");
		else {
			// Generate a BSSID/MAC address
			char dummyMAC[ETHER_ADDR_LEN];
			memset(dummyMAC, entryId, sizeof(dummyMAC));
			// Making sure the 1st 2-bits on the 1st byte of OUI are zero to prevent issue with some games (ie. Gran Turismo)
			dummyMAC[0] &= 0xfc;
			Memory::Memcpy(resultAddr, dummyMAC, sizeof(dummyMAC), "GetBSSDescEntryUser");
		}
		break;
	case PSP_NET_APCTL_DESC_SSID_NAME:
		// Return 32 bytes
		if (entryId == 0)
			Memory::Memcpy(resultAddr, netApctlInfo.ssid, sizeof(netApctlInfo.ssid), "GetBSSDescEntryUser");
		else {
			Memory::Memcpy(resultAddr, dummySSID, sizeof(dummySSID), "GetBSSDescEntryUser");
		}
		break;
	case PSP_NET_APCTL_DESC_SSID_NAME_LENGTH:
		// Return one 32-bit value
		if (entryId == 0)
			Memory::Write_U32(netApctlInfo.ssidLength, resultAddr);
		else {
			// Calculate the SSID length
			Memory::Write_U32((u32)strlen(dummySSID), resultAddr);
		}
		break;
	case PSP_NET_APCTL_DESC_CHANNEL:
		// FIXME: Return one 1 byte value or may be 32-bit if this is not a channel?
		if (entryId == 0)
			Memory::Write_U8(netApctlInfo.channel, resultAddr);
		else {
			// Generate channel for testing purposes, not even sure whether this is channel or not, MGS:PW seems to treat the data as u8
			Memory::Write_U8(entryId, resultAddr);
		}
		break;
	case PSP_NET_APCTL_DESC_SIGNAL_STRENGTH:
		// Return 1 byte
		if (entryId == 0)
			Memory::Write_U8(netApctlInfo.strength, resultAddr);
		else {
			// Randomize signal strength between 1%~99% since games like MGS:PW are using signal strength to determine the strength of the recruit
			Memory::Write_U8((int)(((float)rand() / (float)RAND_MAX) * 99.0 + 1.0), resultAddr);
		}
		break;
	case PSP_NET_APCTL_DESC_SECURITY:
		// Return one 32-bit value
		Memory::Write_U32(netApctlInfo.securityType, resultAddr);
		break;
	default:
		return hleLogError(Log::sceNet, ERROR_NET_APCTL_INVALID_CODE, "unknown info id");
	}

	return 0;
}

static int sceNetApctlGetBSSDescEntryUser(int entryId, int infoId, u32 resultAddr) {
	WARN_LOG(Log::sceNet, "UNTESTED %s(%i, %i, %08x)", __FUNCTION__, entryId, infoId, resultAddr);
	return NetApctl_GetBSSDescEntryUser(entryId, infoId, resultAddr);
}

static int sceNetApctlScanSSID2() {
	WARN_LOG(Log::sceNet, "UNTESTED %s() at %08x", __FUNCTION__, currentMIPS->pc);
	return NetApctl_ScanUser();
}

/**************
* Arg1 = output buffer size being filled? (initially the same with Arg3 ?)
* Arg2 = output buffer? (linked list where the 1st 32-bit is the next address? followed by entry Id? ie. 8-bytes per entry?)
* Arg3 = max buffer size? (ie. 0x100 ?)
* Arg4 = input flag? (initially 0/1 ?)
***************/
static int sceNetApctlGetBSSDescIDList2(u32 Arg1, u32 Arg2, u32 Arg3, u32 Arg4) {
	WARN_LOG(Log::sceNet, "UNTESTED %s(%08x, %08x, %08x, %08x) at %08x", __FUNCTION__, Arg1, Arg2, Arg3, Arg4, currentMIPS->pc);
	return NetApctl_GetBSSDescIDListUser(Arg1, Arg2);
}

/**************
* Arg1 = a value returned from sceNetApctlGetBSSDescIDList2 ? entryId?
* Arg2 = input field type within the entry desc (ie. PSP_NET_APCTL_DESC_SSID_NAME ?)
* Arg3 = output buffer for retrieved entry data? (max size = 32 bytes? ie. APCTL_SSID_MAXLEN ? or similar to SceNetApctlInfoInternal union ?)
***************/
static int sceNetApctlGetBSSDescEntry2(int entryId, int infoId, u32 resultAddr) {
	WARN_LOG(Log::sceNet, "UNTESTED %s(%i, %i, %08x) at %08x", __FUNCTION__, entryId, infoId, resultAddr, currentMIPS->pc);
	return NetApctl_GetBSSDescEntryUser(entryId, infoId, resultAddr);
}

static int sceNetResolverInit()
{
	ERROR_LOG(Log::sceNet, "UNIMPL %s()", __FUNCTION__);
	return 0;
}

static int sceNetApctlAddInternalHandler(u32 handlerPtr, u32 handlerArg) {
	ERROR_LOG(Log::sceNet, "UNIMPL %s(%08x, %08x)", __FUNCTION__, handlerPtr, handlerArg);
	// This seems to be a 2nd kind of handler
	return NetApctl_AddHandler(handlerPtr, handlerArg);
}

static int sceNetApctlDelInternalHandler(u32 handlerID) {
	ERROR_LOG(Log::sceNet, "UNIMPL %s(%i)", __FUNCTION__, handlerID);
	// This seems to be a 2nd kind of handler
	return NetApctl_DelHandler(handlerID);
}

static int sceNetApctl_A7BB73DF(u32 handlerPtr, u32 handlerArg) {
	ERROR_LOG(Log::sceNet, "UNIMPL %s(%08x, %08x)", __FUNCTION__, handlerPtr, handlerArg);
	// This seems to be a 3rd kind of handler
	return sceNetApctlAddHandler(handlerPtr, handlerArg);
}

static int sceNetApctl_6F5D2981(u32 handlerID) {
	ERROR_LOG(Log::sceNet, "UNIMPL %s(%i)", __FUNCTION__, handlerID);
	// This seems to be a 3rd kind of handler
	return sceNetApctlDelHandler(handlerID);
}

static int sceNetApctl_lib2_69745F0A(int handlerId) {
	return hleLogError(Log::sceNet, 0, "unimplemented");
}

static int sceNetApctl_lib2_4C19731F(int code, u32 pInfoAddr) {
	ERROR_LOG(Log::sceNet, "UNIMPL %s(%i, %08x)", __FUNCTION__, code, pInfoAddr);
	return sceNetApctlGetInfo(code, pInfoAddr);
}

static int sceNetApctlScan() {
	ERROR_LOG(Log::sceNet, "UNIMPL %s()", __FUNCTION__);
	return NetApctl_ScanUser();
}

static int sceNetApctlGetBSSDescIDList(u32 sizeAddr, u32 bufAddr) {
	ERROR_LOG(Log::sceNet, "UNIMPL %s(%08x, %08x)", __FUNCTION__, sizeAddr, bufAddr);
	return sceNetApctlGetBSSDescIDListUser(sizeAddr, bufAddr);
}

static int sceNetApctlGetBSSDescEntry(int entryId, int infoId, u32 resultAddr) {
	ERROR_LOG(Log::sceNet, "UNIMPL %s(%i, %i, %08x)", __FUNCTION__, entryId, infoId, resultAddr);
	return sceNetApctlGetBSSDescEntryUser(entryId, infoId, resultAddr);
}

static int sceNetApctl_lib2_C20A144C(int connIndex, u32 ps3MacAddressPtr) {
	ERROR_LOG(Log::sceNet, "UNIMPL %s(%i, %08x)", __FUNCTION__, connIndex, ps3MacAddressPtr);
	return sceNetApctlConnect(connIndex);
}


static int sceNetUpnpInit(int unknown1,int unknown2)
{
	ERROR_LOG_REPORT_ONCE(sceNetUpnpInit, Log::sceNet, "UNIMPLsceNetUpnpInit %d,%d",unknown1,unknown2);
	return 0;
}

static int sceNetUpnpStart()
{
	ERROR_LOG(Log::sceNet, "UNIMPLsceNetUpnpStart");
	return 0;
}

static int sceNetUpnpStop()
{
	ERROR_LOG(Log::sceNet, "UNIMPLsceNetUpnpStop");
	return 0;
}

static int sceNetUpnpTerm()
{
	ERROR_LOG(Log::sceNet, "UNIMPLsceNetUpnpTerm");
	return 0;
}

static int sceNetUpnpGetNatInfo()
{
	ERROR_LOG(Log::sceNet, "UNIMPLsceNetUpnpGetNatInfo");
	return 0;
}

static int sceNetGetDropRate(u32 dropRateAddr, u32 dropDurationAddr)
{
	Memory::Write_U32(netDropRate, dropRateAddr);
	Memory::Write_U32(netDropDuration, dropDurationAddr);
	return hleLogSuccessInfoI(Log::sceNet, 0);
}

static int sceNetSetDropRate(u32 dropRate, u32 dropDuration)
{
	netDropRate = dropRate;
	netDropDuration = dropDuration;
	return hleLogSuccessInfoI(Log::sceNet, 0);
}

const HLEFunction sceNet[] = {
	{0X39AF39A6, &WrapI_UUUUU<sceNetInit>,           "sceNetInit",                      'i', "xxxxx"},
	{0X281928A9, &WrapU_V<sceNetTerm>,               "sceNetTerm",                      'x', ""     },
	{0X89360950, &WrapV_UU<sceNetEtherNtostr>,       "sceNetEtherNtostr",               'v', "xx"   },
	{0XD27961C9, &WrapV_UU<sceNetEtherStrton>,       "sceNetEtherStrton",               'v', "xx"   },
	{0X0BF0A3AE, &WrapU_U<sceNetGetLocalEtherAddr>,  "sceNetGetLocalEtherAddr",         'x', "x"    },
	{0X50647530, &WrapI_I<sceNetFreeThreadinfo>,     "sceNetFreeThreadinfo",            'i', "i"    },
	{0XCC393E48, &WrapI_U<sceNetGetMallocStat>,      "sceNetGetMallocStat",             'i', "p"    },
	{0XAD6844C6, &WrapI_I<sceNetThreadAbort>,        "sceNetThreadAbort",               'i', "i"    },
};

const HLEFunction sceNetResolver[] = {
	{0X224C5F44, nullptr,                            "sceNetResolverStartNtoA",         '?', ""     },
	{0X244172AF, nullptr,                            "sceNetResolverCreate",            '?', ""     },
	{0X94523E09, nullptr,                            "sceNetResolverDelete",            '?', ""     },
	{0XF3370E61, &WrapI_V<sceNetResolverInit>,       "sceNetResolverInit",              'i', ""     },
	{0X808F6063, nullptr,                            "sceNetResolverStop",              '?', ""     },
	{0X6138194A, nullptr,                            "sceNetResolverTerm",              '?', ""     },
	{0X629E2FB7, nullptr,                            "sceNetResolverStartAtoN",         '?', ""     },
	{0X14C17EF9, nullptr,                            "sceNetResolverStartNtoAAsync",    '?', ""     },
	{0XAAC09184, nullptr,                            "sceNetResolverStartAtoNAsync",    '?', ""     },
	{0X12748EB9, nullptr,                            "sceNetResolverWaitAsync",         '?', ""     },
	{0X4EE99358, nullptr,                            "sceNetResolverPollAsync",         '?', ""     },
};					 

const HLEFunction sceNetInet[] = {
	{0X17943399, &WrapI_V<sceNetInetInit>,           "sceNetInetInit",                  'i', ""     },
	{0X4CFE4E56, nullptr,                            "sceNetInetShutdown",              '?', ""     },
	{0XA9ED66B9, &WrapI_V<sceNetInetTerm>,           "sceNetInetTerm",                  'i', ""     },
	{0X8B7B220F, &WrapI_III<sceNetInetSocket>,       "sceNetInetSocket",                'i', "iii"  },
	{0X2FE71FE7, &WrapI_IIIUI<sceNetInetSetsockopt>, "sceNetInetSetsockopt",            'i', "iiixi"},
	{0X4A114C7C, nullptr,                            "sceNetInetGetsockopt",            '?', ""     },
	{0X410B34AA, &WrapI_IUI<sceNetInetConnect>,      "sceNetInetConnect",               'i', "ixi"  },
	{0X805502DD, nullptr,                            "sceNetInetCloseWithRST",          '?', ""     },
	{0XD10A1A7A, nullptr,                            "sceNetInetListen",                '?', ""     },
	{0XDB094E1B, nullptr,                            "sceNetInetAccept",                '?', ""     },
	{0XFAABB1DD, &WrapI_VUI<sceNetInetPoll>,         "sceNetInetPoll",                  'i', "pxi"  },
	{0X5BE8D595, nullptr,                            "sceNetInetSelect",                '?', ""     },
	{0X8D7284EA, nullptr,                            "sceNetInetClose",                 '?', ""     },
	{0XCDA85C99, &WrapI_IUUU<sceNetInetRecv>,        "sceNetInetRecv",                  'i', "ixxx" },
	{0XC91142E4, nullptr,                            "sceNetInetRecvfrom",              '?', ""     },
	{0XEECE61D2, nullptr,                            "sceNetInetRecvmsg",               '?', ""     },
	{0X7AA671BC, &WrapI_IUUU<sceNetInetSend>,        "sceNetInetSend",                  'i', "ixxx" },
	{0X05038FC7, nullptr,                            "sceNetInetSendto",                '?', ""     },
	{0X774E36F4, nullptr,                            "sceNetInetSendmsg",               '?', ""     },
	{0XFBABE411, &WrapI_V<sceNetInetGetErrno>,       "sceNetInetGetErrno",              'i', ""     },
	{0X1A33F9AE, nullptr,                            "sceNetInetBind",                  '?', ""     },
	{0XB75D5B0A, nullptr,                            "sceNetInetInetAddr",              '?', ""     },
	{0X1BDF5D13, &WrapI_CU<sceNetInetInetAton>,      "sceNetInetInetAton",              'i', "sx"   },
	{0XD0792666, nullptr,                            "sceNetInetInetNtop",              '?', ""     },
	{0XE30B8C19, nullptr,                            "sceNetInetInetPton",              '?', ""     },
	{0X8CA3A97E, nullptr,                            "sceNetInetGetPspError",           '?', ""     },
	{0XE247B6D6, nullptr,                            "sceNetInetGetpeername",           '?', ""     },
	{0X162E6FD5, nullptr,                            "sceNetInetGetsockname",           '?', ""     },
	{0X80A21ABD, nullptr,                            "sceNetInetSocketAbort",           '?', ""     },
	{0X39B0C7D3, nullptr,                            "sceNetInetGetUdpcbstat",          '?', ""     },
	{0XB3888AD4, nullptr,                            "sceNetInetGetTcpcbstat",          '?', ""     },
};

const HLEFunction sceNetApctl[] = {
	{0XCFB957C6, &WrapI_I<sceNetApctlConnect>,       "sceNetApctlConnect",              'i', "i"    },
	{0X24FE91A1, &WrapI_V<sceNetApctlDisconnect>,    "sceNetApctlDisconnect",           'i', ""     },
	{0X5DEAC81B, &WrapI_U<sceNetApctlGetState>,      "sceNetApctlGetState",             'i', "x"    },
	{0X8ABADD51, &WrapU_UU<sceNetApctlAddHandler>,   "sceNetApctlAddHandler",           'x', "xx"   },
	{0XE2F91F9B, &WrapI_II<sceNetApctlInit>,          "sceNetApctlInit",                'i', "ii"   },
	{0X5963991B, &WrapI_U<sceNetApctlDelHandler>,    "sceNetApctlDelHandler",           'i', "x"    },
	{0XB3EDD0EC, &WrapI_V<sceNetApctlTerm>,          "sceNetApctlTerm",                 'i', ""     },
	{0X2BEFDF23, &WrapI_IU<sceNetApctlGetInfo>,      "sceNetApctlGetInfo",              'i', "ix"   },
	{0XA3E77E13, &WrapI_V<sceNetApctlScanSSID2>,     "sceNetApctlScanSSID2",            'i', ""     },
	{0XE9B2E5E6, &WrapI_V<sceNetApctlScanUser>,                 "sceNetApctlScanUser",             'i', ""     },
	{0XF25A5006, &WrapI_UUUU<sceNetApctlGetBSSDescIDList2>,     "sceNetApctlGetBSSDescIDList2",    'i', "xxxx" },
	{0X2935C45B, &WrapI_IIU<sceNetApctlGetBSSDescEntry2>,       "sceNetApctlGetBSSDescEntry2",     'i', "iix"  },
	{0X04776994, &WrapI_IIU<sceNetApctlGetBSSDescEntryUser>,    "sceNetApctlGetBSSDescEntryUser",  'i', "iix"  },
	{0X6BDDCB8C, &WrapI_UU<sceNetApctlGetBSSDescIDListUser>,    "sceNetApctlGetBSSDescIDListUser", 'i', "xx"   },
	{0X7CFAB990, &WrapI_UU<sceNetApctlAddInternalHandler>,      "sceNetApctlAddInternalHandler",   'i', "xx"   },
	{0XE11BAFAB, &WrapI_U<sceNetApctlDelInternalHandler>,       "sceNetApctlDelInternalHandler",   'i', "x"    },
	{0XA7BB73DF, &WrapI_UU<sceNetApctl_A7BB73DF>,               "sceNetApctl_A7BB73DF",            'i', "xx"   },
	{0X6F5D2981, &WrapI_U<sceNetApctl_6F5D2981>,                "sceNetApctl_6F5D2981",            'i', "x"    },
	{0X69745F0A, &WrapI_I<sceNetApctl_lib2_69745F0A>,           "sceNetApctl_lib2_69745F0A",       'i', "i"    },
	{0X4C19731F, &WrapI_IU<sceNetApctl_lib2_4C19731F>,          "sceNetApctl_lib2_4C19731F",       'i', "ix"   },
	{0XB3CF6849, &WrapI_V<sceNetApctlScan>,                     "sceNetApctlScan",                 'i', ""     },
	{0X0C7FFA5C, &WrapI_UU<sceNetApctlGetBSSDescIDList>,        "sceNetApctlGetBSSDescIDList",     'i', "xx"   },
	{0X96BEB231, &WrapI_IIU<sceNetApctlGetBSSDescEntry>,        "sceNetApctlGetBSSDescEntry",      'i', "iix"  },
	{0XC20A144C, &WrapI_IU<sceNetApctl_lib2_C20A144C>,          "sceNetApctl_lib2_C20A144C",       'i', "ix"   },
	// Fake function for PPSSPP's use.
	{0X756E6F10, &WrapV_V<__NetApctlCallbacks>,                 "__NetApctlCallbacks",             'v', ""     },
};

const HLEFunction sceWlanDrv[] = {
	{0XD7763699, &WrapU_V<sceWlanGetSwitchState>,    "sceWlanGetSwitchState",           'x', ""     },
	{0X0C622081, &WrapU_U<sceWlanGetEtherAddr>,      "sceWlanGetEtherAddr",             'x', "x"    },
	{0X93440B11, &WrapU_V<sceWlanDevIsPowerOn>,      "sceWlanDevIsPowerOn",             'x', ""     },
};

// see http://www.kingx.de/forum/showthread.php?tid=35164
const HLEFunction sceNetUpnp[] = {
	{0X27045362, &WrapI_V<sceNetUpnpGetNatInfo>,     "sceNetUpnpGetNatInfo",            'i', ""     },
	{0X3432B2E5, &WrapI_V<sceNetUpnpStart>,          "sceNetUpnpStart",                 'i', ""     },
	{0X3E32ED9E, &WrapI_V<sceNetUpnpStop>,           "sceNetUpnpStop",                  'i', ""     },
	{0X540491EF, &WrapI_V<sceNetUpnpTerm>,           "sceNetUpnpTerm",                  'i', ""     },
	{0XE24220B5, &WrapI_II<sceNetUpnpInit>,          "sceNetUpnpInit",                  'i', "ii"   },
};

const HLEFunction sceNetIfhandle[] = {
	{0xC80181A2, &WrapI_UU<sceNetGetDropRate>,     "sceNetGetDropRate",                 'i', "pp" },
	{0xFD8585E1, &WrapI_UU<sceNetSetDropRate>,     "sceNetSetDropRate",                 'i', "ii" },
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

void Register_sceNetUpnp() {
	RegisterModule("sceNetUpnp", ARRAY_SIZE(sceNetUpnp), sceNetUpnp);
}

void Register_sceNetIfhandle() {
	RegisterModule("sceNetIfhandle", ARRAY_SIZE(sceNetIfhandle), sceNetIfhandle);
}
