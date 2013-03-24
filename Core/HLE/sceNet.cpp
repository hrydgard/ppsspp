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
#include "sceUtility.h"

static bool netInited;
static bool netAdhocInited;

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

	ERROR_NET_ADHOCCTL_ALREADY_INITIALIZED       = 0x80410b07,
	ERROR_NET_ADHOCCTL_NOT_INITIALIZED           = 0x80410b08,
	ERROR_NET_ADHOCCTL_TOO_MANY_HANDLERS         = 0x80410b12,
};

void __NetInit() {
	netInited = false;
	netAdhocInited = false;
}


void __NetShutdown() {

}

// This feels like a dubious proposition, mostly...
void __NetDoState(PointerWrap &p) {
	p.Do(netInited);
	p.Do(netAdhocInited);
	p.DoMarker("net");
}

void sceNetInit() {
	ERROR_LOG(HLE,"UNIMPL sceNetInit(poolsize=%d, calloutpri=%i, calloutstack=%d, netintrpri=%i, netintrstack=%d)", PARAM(0), PARAM(1), PARAM(2), PARAM(3), PARAM(4));
	netInited = true;
	RETURN(0); //ERROR
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
	ERROR_LOG(HLE,"UNIMPL sceNetAdhocInit(%i, %i, %08x)", stackSize, prio, productAddr);
	return 0;
}

u32 sceWlanGetEtherAddr(u32 addrAddr)
{
	static const u8 fakeEtherAddr[6] = { 1, 2, 3, 4, 5, 6 };
	DEBUG_LOG(HLE, "sceWlanGetEtherAddr(%08x)", addrAddr);
	for (int i = 0; i < 6; i++)
		Memory::Write_U8(fakeEtherAddr[i], addrAddr + i);
	return 0;
}

u32 sceWlanDevIsPowerOn()
{
	DEBUG_LOG(HLE, "UNTESTED 0=sceWlanDevIsPowerOn()");
	return 0;
}

u32 sceWlanGetSwitchState() {
	DEBUG_LOG(HLE, "UNTESTED sceWlanGetSwitchState()");
	return 0;
}

const HLEFunction sceNet[] =
{
	{0x39AF39A6, sceNetInit, "sceNetInit"},
	{0x281928A9, WrapU_V<sceNetTerm>, "sceNetTerm"},
	{0x89360950, 0, "sceNetEtherNtostr"}, 
	{0x0bf0a3ae, 0, "sceNetGetLocalEtherAddr"}, 
	{0xd27961c9, 0, "sceNetEtherStrton"}, 
	{0x50647530, 0, "sceNetFreeThreadinfo"}, 
	{0xcc393e48, 0, "sceNetGetMallocStat"},
};

const HLEFunction sceNetAdhoc[] =
{
	{0xE1D621D7, WrapU_V<sceNetAdhocInit>, "sceNetAdhocInit"}, 
	{0xA62C6F57, 0, "sceNetAdhocTerm"}, 
	{0x0AD043ED, 0, "sceNetAdhocctlConnect"},
	{0x6f92741b, 0, "sceNetAdhocPdpCreate"},
	{0xabed3790, 0, "sceNetAdhocPdpSend"},
	{0xdfe53e03, 0, "sceNetAdhocPdpRecv"},
	{0x7f27bb5e, 0, "sceNetAdhocPdpDelete"},
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
	{0x73bfd52d, 0, "sceNetAdhocSetSocketAlert"},
	{0x7a662d6b, 0, "sceNetAdhocPollSocket"},
};							

int sceNetAdhocMatchingInit(u32 memsize) {
	ERROR_LOG(HLE, "UNIMPL sceNetAdhocMatchingInit(%08x)", memsize);
	return 0;
}

const HLEFunction sceNetAdhocMatching[] = 
{
	{0x2a2a1e07, WrapI_U<sceNetAdhocMatchingInit>, "sceNetAdhocMatchingInit"},
	{0x7945ecda, 0, "sceNetAdhocMatchingTerm"},
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
};

const HLEFunction sceNetAdhocctl[] =
{
	{0xE26F226E, WrapU_IIU<sceNetAdhocctlInit>, "sceNetAdhocctlInit"},
	{0x9D689E13, 0, "sceNetAdhocctlTerm"},
	{0x20B317A0, 0, "sceNetAdhocctlAddHandler"},
	{0x6402490B, 0, "sceNetAdhocctlDelHandler"},
	{0x34401D65, 0, "sceNetAdhocctlDisconnect"},
	{0x0ad043ed, 0, "sceNetAdhocctlConnect"},
	{0x08fff7a0, 0, "sceNetAdhocctlScan"},
	{0x75ecd386, 0, "sceNetAdhocctlGetNameByAddr"},
	{0x8916c003, 0, "sceNetAdhocctlGetNameByAddr"},
	{0xded9d28e, 0, "sceNetAdhocctlGetParameter"},
	{0x81aee1be, 0, "sceNetAdhocctlGetScanInfo"},
	{0x5e7f79c9, 0, "sceNetAdhocctlJoin"},
	{0x8db83fdc, 0, "sceNetAdhocctlGetPeerInfo"},
	{0xec0635c1, 0, "sceNetAdhocctlCreate"},
	{0xa5c055ce, 0, "sceNetAdhocctlCreateEnterGameMode"},
	{0x1ff89745, 0, "sceNetAdhocctlJoinEnterGameMode"},
	{0xcf8e084d, 0, "sceNetAdhocctlExitGameMode"},
	{0xe162cb14, 0, "sceNetAdhocctlGetPeerList"},
	{0x362cbe8f, 0, "sceNetAdhocctlGetAdhocId"},
};

const HLEFunction sceNetResolver[] =
{
	{0x224c5f44, 0, "sceNetResolverStartNtoA"},
	{0x244172af, 0, "sceNetResolverCreate"},
	{0x94523e09, 0, "sceNetResolverDelete"},
	{0xf3370e61, 0, "sceNetResolverInit"},
	{0x808F6063, 0, "sceNetResolverStop"},
	{0x6138194A, 0, "sceNetResolverTermFunction"},
};					 

const HLEFunction sceNetInet[] = 
{
	{0x17943399, 0, "sceNetInetInit"},
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
	{0xa9ed66b9, 0, "sceNetInetTerm"},
	{0xE30B8C19, 0, "sceNetInetInetPton"},
	{0xE247B6D6, 0, "sceNetInetGetpeername"},
	{0x162e6fd5, 0, "sceNetInetGetsockname"},
	{0x4a114c7c, 0, "sceNetInetGetsockopt"}, 
	{0xfaabb1dd, 0, "sceNetInetPoll"},
	{0x1BDF5D13, 0, "sceNetInetInetAton"},
	{0x80A21ABD, 0, "sceNetInetSocketAbort"},
	{0x805502DD, 0, "sceNetInetCloseWithRST"},
};

const HLEFunction sceNetApctl[] = 
{
	{0xCFB957C6, 0, "sceNetApctlConnect"},
	{0x24fe91a1, 0, "sceNetApctlDisconnect"},
	{0x5deac81b, 0, "sceNetApctlGetState"},
	{0x8abadd51, 0, "sceNetApctlAddHandler"},
	{0xe2f91f9b, 0, "sceNetApctlInitFunction"},
	{0x5963991b, 0, "sceNetApctlDelHandler"},
	{0xb3edd0ec, 0, "sceNetApctlTerm"},
	{0x2BEFDF23, 0, "sceNetApctlGetInfo"},
};

const HLEFunction sceWlanDrv[] =
{
	{0xd7763699, WrapU_V<sceWlanGetSwitchState>, "sceWlanGetSwitchState"},
	{0x0c622081, WrapU_U<sceWlanGetEtherAddr>, "sceWlanGetEtherAddr"},
	{0x93440B11, WrapU_V<sceWlanDevIsPowerOn>, "sceWlanDevIsPowerOn"},
};

void Register_sceNet()
{
	RegisterModule("sceNet", ARRAY_SIZE(sceNet), sceNet);
	RegisterModule("sceNetAdhoc", ARRAY_SIZE(sceNetAdhoc), sceNetAdhoc);
	RegisterModule("sceNetAdhocMatching", ARRAY_SIZE(sceNetAdhocMatching), sceNetAdhocMatching);
	RegisterModule("sceNetAdhocctl", ARRAY_SIZE(sceNetAdhocctl), sceNetAdhocctl);
	RegisterModule("sceNetResolver", ARRAY_SIZE(sceNetResolver), sceNetResolver);
	RegisterModule("sceNetInet", ARRAY_SIZE(sceNetInet), sceNetInet);
	RegisterModule("sceNetApctl", ARRAY_SIZE(sceNetApctl), sceNetApctl);
}

void Register_sceWlanDrv()
{
	RegisterModule("sceWlanDrv", ARRAY_SIZE(sceWlanDrv), sceWlanDrv);
}
