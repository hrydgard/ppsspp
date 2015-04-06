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
#include "Core/MemMapHelpers.h"

#include "sceKernel.h"
#include "sceKernelThread.h"
#include "sceKernelMutex.h"
#include "sceUtility.h"

#include "Core/HLE/proAdhoc.h"
#include "Core/HLE/sceNetAdhoc.h"
#include "Core/HLE/sceNet.h"
#include "Core/Reporting.h"

static bool netInited;
static bool netInetInited;
static bool netApctlInited;

static struct SceNetMallocStat netMallocStat;

static std::map<int, ApctlHandler> apctlHandlers;

static void __ResetInitNetLib() {
	netInited = false;
	netApctlInited = false;
	netInetInited = false;

	memset(&netMallocStat, 0, sizeof(netMallocStat));
}

void __NetInit() {
	//net::Init();
#ifdef _MSC_VER
	WSADATA data;
	int iResult = WSAStartup(MAKEWORD(2, 2), &data);
	if (iResult != NOERROR){
		ERROR_LOG(SCENET, "WSA Failed");
	}
#endif
	__ResetInitNetLib();
}

void __NetShutdown() {
	__ResetInitNetLib();
	//net::Shutdown();
#ifdef _MSC_VER
	WSACleanup();
#endif
}


static void __UpdateApctlHandlers(int oldState, int newState, int flag, int error) {
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

static u32 sceNetTerm() {
	//May also need to Terminate netAdhocctl and netAdhoc since the game (ie. GTA:VCS, Wipeout Pulse, etc) might not called them before calling sceNetTerm and causing them to behave strangely on the next sceNetInit+sceNetAdhocInit
	if (netAdhocctlInited) sceNetAdhocctlTerm();
	if (netAdhocInited) sceNetAdhocTerm();

	WARN_LOG(SCENET, "sceNetTerm()");
	netInited = false;

	return 0;
}

// TODO: should that struct actually be initialized here?
static u32 sceNetInit(u32 poolSize, u32 calloutPri, u32 calloutStack, u32 netinitPri, u32 netinitStack)  {
	// May need to Terminate old one first since the game (ie. GTA:VCS) might not called sceNetTerm before the next sceNetInit and behave strangely
	if (netInited) 
		sceNetTerm();

	WARN_LOG(SCENET, "sceNetInit(poolsize=%d, calloutpri=%i, calloutstack=%d, netintrpri=%i, netintrstack=%d) at %08x", poolSize, calloutPri, calloutStack, netinitPri, netinitStack, currentMIPS->pc);
	netInited = true;
	netMallocStat.maximum = poolSize;
	netMallocStat.free = poolSize;
	netMallocStat.pool = 0;
	
	return 0;
}

static u32 sceWlanGetEtherAddr(u32 addrAddr) {
	// Read MAC Address from config
	uint8_t mac[6] = {0};
	if (!ParseMacAddress(g_Config.sMACAddress.c_str(), mac)) {
		ERROR_LOG(SCENET, "Error parsing mac address %s", g_Config.sMACAddress.c_str());
	}
	DEBUG_LOG(SCENET, "sceWlanGetEtherAddr(%08x)", addrAddr);
	for (int i = 0; i < 6; i++)
		Memory::Write_U8(mac[i], addrAddr + i);
	return 0;
}

static u32 sceNetGetLocalEtherAddr(u32 addrAddr) {
	return sceWlanGetEtherAddr(addrAddr);
}

static u32 sceWlanDevIsPowerOn() {
	DEBUG_LOG(SCENET, "UNTESTED sceWlanDevIsPowerOn()");
	return g_Config.bEnableWlan ? 1 : 0;
}

static u32 sceWlanGetSwitchState() {
	VERBOSE_LOG(SCENET, "sceWlanGetSwitchState()");
	return g_Config.bEnableWlan ? 1 : 0;
}

// Probably a void function, but often returns a useful value.
static int sceNetEtherNtostr(u32 macPtr, u32 bufferPtr) {
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
static int sceNetEtherStrton(u32 bufferPtr, u32 macPtr) {
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
static int sceNetGetMallocStat(u32 statPtr) {
	WARN_LOG(SCENET, "UNTESTED sceNetGetMallocStat(%x)", statPtr);
	if(Memory::IsValidAddress(statPtr))
		Memory::WriteStruct(statPtr, &netMallocStat);
	else
		ERROR_LOG(SCENET, "UNTESTED sceNetGetMallocStat(%x): tried to request invalid address!", statPtr);

	return 0;
}

static int sceNetInetInit() {
	ERROR_LOG(SCENET, "UNIMPL sceNetInetInit()");
	if (netInetInited) return ERROR_NET_INET_ALREADY_INITIALIZED;
	netInetInited = true;

	return 0;
}

static int sceNetInetTerm() {
	ERROR_LOG(SCENET, "UNIMPL sceNetInetTerm()");
	netInetInited = false;

	return 0;
}

static int sceNetApctlInit() {
	ERROR_LOG(SCENET, "UNIMPL sceNetApctlInit()");
	if (netApctlInited)
		return ERROR_NET_APCTL_ALREADY_INITIALIZED;
	netApctlInited = true;

	return 0;
}

static int sceNetApctlTerm() {
	ERROR_LOG(SCENET, "UNIMPL sceNeApctlTerm()");
	netApctlInited = false;
	
	return 0;
}

// TODO: How many handlers can the PSP actually have for Apctl?
// TODO: Should we allow the same handler to be added more than once?
static u32 sceNetApctlAddHandler(u32 handlerPtr, u32 handlerArg) {
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
	else {
		ERROR_LOG(SCENET, "UNTESTED sceNetApctlAddHandler(%x, %x): Same handler already exists", handlerPtr, handlerArg);
	}

	// The id to return is the number of handlers currently registered
	return retval;
}

static int sceNetApctlDelHandler(u32 handlerID) {
	if(apctlHandlers.find(handlerID) != apctlHandlers.end()) {
		apctlHandlers.erase(handlerID);
		WARN_LOG(SCENET, "UNTESTED sceNetapctlDelHandler(%d): deleted handler %d", handlerID, handlerID);
	}
	else {
		ERROR_LOG(SCENET, "UNTESTED sceNetapctlDelHandler(%d): asked to delete invalid handler %d", handlerID, handlerID);
	}
	return 0;
}

static int sceNetInetInetAton(const char *hostname, u32 addrPtr) {
	ERROR_LOG(SCENET, "UNIMPL sceNetInetInetAton(%s, %08x)", hostname, addrPtr);
	return -1;
}

int sceNetInetPoll(void *fds, u32 nfds, int timeout) { // timeout in miliseconds
	DEBUG_LOG(SCENET, "UNTESTED sceNetInetPoll(%p, %d, %i) at %08x", fds, nfds, timeout, currentMIPS->pc);
	int retval = -1;
	SceNetInetPollfd *fdarray = (SceNetInetPollfd *)fds; // SceNetInetPollfd/pollfd, sceNetInetPoll() have similarity to BSD poll() but pollfd have different size on 64bit
//#ifdef _MSC_VER
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
	ERROR_LOG(SCENET, "UNIMPL sceNetInetRecv(%i, %08x, %i, %08x)", socket, bufPtr, bufLen, flags);
	return -1;
}

static int sceNetInetSend(int socket, u32 bufPtr, u32 bufLen, u32 flags) {
	ERROR_LOG(SCENET, "UNIMPL sceNetInetSend(%i, %08x, %i, %08x)", socket, bufPtr, bufLen, flags);
	return -1;
}

static int sceNetInetGetErrno() {
	ERROR_LOG(SCENET, "UNTESTED sceNetInetGetErrno()");
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
	ERROR_LOG(SCENET, "UNIMPL sceNetInetSocket(%i, %i, %i)", domain, type, protocol);
	return -1;
}

static int sceNetInetSetsockopt(int socket, int level, int optname, u32 optvalPtr, int optlen) {
	ERROR_LOG(SCENET, "UNIMPL sceNetInetSetsockopt(%i, %i, %i, %08x, %i)", socket, level, optname, optvalPtr, optlen);
	return -1;
}

static int sceNetInetConnect(int socket, u32 sockAddrInternetPtr, int addressLength) {
	ERROR_LOG(SCENET, "UNIMPL sceNetInetConnect(%i, %08x, %i)", socket, sockAddrInternetPtr, addressLength);
	return -1;
}

static int sceNetApctlDisconnect() {
	ERROR_LOG(SCENET, "UNIMPL %s()", __FUNCTION__);
	// Like its 'sister' function sceNetAdhocctlDisconnect, we need to alert Apctl handlers that a disconnect took place
	// or else games like Phantasy Star Portable 2 will hang at certain points (e.g. returning to the main menu after trying to connect to PSN).
	__UpdateApctlHandlers(0, 0, PSP_NET_APCTL_EVENT_DISCONNECT_REQUEST, 0);
	return 0;
}

static int sceNetResolverInit()
{
	ERROR_LOG(SCENET, "UNIMPL %s()", __FUNCTION__);
	return 0;
}


static int sceNetUpnpInit(int unknown1,int unknown2)
{
	ERROR_LOG_REPORT_ONCE(sceNetUpnpInit, SCENET, "UNIMPLsceNetUpnpInit %d,%d",unknown1,unknown2);	
	return 0;
}

static int sceNetUpnpStart()
{
	ERROR_LOG(SCENET, "UNIMPLsceNetUpnpStart");
	return 0;
}

static int sceNetUpnpStop()
{
	ERROR_LOG(SCENET, "UNIMPLsceNetUpnpStop");
	return 0;
}

static int sceNetUpnpTerm()
{
	ERROR_LOG(SCENET, "UNIMPLsceNetUpnpTerm");
	return 0;
}

static int sceNetUpnpGetNatInfo()
{
	ERROR_LOG(SCENET, "UNIMPLsceNetUpnpGetNatInfo");
	return 0;
}

const HLEFunction sceNet[] = {
	{0X39AF39A6, &WrapU_UUUUU<sceNetInit>,           "sceNetInit",                      'x', "xxxxx"},
	{0X281928A9, &WrapU_V<sceNetTerm>,               "sceNetTerm",                      'x', ""     },
	{0X89360950, &WrapI_UU<sceNetEtherNtostr>,       "sceNetEtherNtostr",               'i', "xx"   },
	{0XD27961C9, &WrapI_UU<sceNetEtherStrton>,       "sceNetEtherStrton",               'i', "xx"   },
	{0X0BF0A3AE, &WrapU_U<sceNetGetLocalEtherAddr>,  "sceNetGetLocalEtherAddr",         'x', "x"    },
	{0X50647530, nullptr,                            "sceNetFreeThreadinfo",            '?', ""     },
	{0XCC393E48, &WrapI_U<sceNetGetMallocStat>,      "sceNetGetMallocStat",             'i', "x"    },
	{0XAD6844C6, nullptr,                            "sceNetThreadAbort",               '?', ""     },
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
	{0XCFB957C6, nullptr,                            "sceNetApctlConnect",              '?', ""     },
	{0X24FE91A1, &WrapI_V<sceNetApctlDisconnect>,    "sceNetApctlDisconnect",           'i', ""     },
	{0X5DEAC81B, nullptr,                            "sceNetApctlGetState",             '?', ""     },
	{0X8ABADD51, &WrapU_UU<sceNetApctlAddHandler>,   "sceNetApctlAddHandler",           'x', "xx"   },
	{0XE2F91F9B, &WrapI_V<sceNetApctlInit>,          "sceNetApctlInit",                 'i', ""     },
	{0X5963991B, &WrapI_U<sceNetApctlDelHandler>,    "sceNetApctlDelHandler",           'i', "x"    },
	{0XB3EDD0EC, &WrapI_V<sceNetApctlTerm>,          "sceNetApctlTerm",                 'i', ""     },
	{0X2BEFDF23, nullptr,                            "sceNetApctlGetInfo",              '?', ""     },
	{0XA3E77E13, nullptr,                            "sceNetApctlScanSSID2",            '?', ""     },
	{0XE9B2E5E6, nullptr,                            "sceNetApctlScanUser",             '?', ""     },
	{0XF25A5006, nullptr,                            "sceNetApctlGetBSSDescIDList2",    '?', ""     },
	{0X2935C45B, nullptr,                            "sceNetApctlGetBSSDescEntry2",     '?', ""     },
	{0X04776994, nullptr,                            "sceNetApctlGetBSSDescEntryUser",  '?', ""     },
	{0X6BDDCB8C, nullptr,                            "sceNetApctlGetBSSDescIDListUser", '?', ""     },
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
