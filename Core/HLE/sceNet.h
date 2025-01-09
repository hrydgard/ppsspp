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

#pragma once

#include "Core/HLE/proAdhoc.h"

#include "Core/HLE/NetInetConstants.h"

// Using constants instead of numbers for readability reason, since PSP_THREAD_ATTR_KERNEL/USER is located in sceKernelThread.cpp instead of sceKernelThread.h
#ifndef PSP_THREAD_ATTR_KERNEL
#define PSP_THREAD_ATTR_KERNEL 0x00001000
#endif
#ifndef PSP_THREAD_ATTR_USER
#define PSP_THREAD_ATTR_USER 0x80000000
#endif

// Option Names
#define PSP_SO_REUSEPORT		0x0200
#define PSP_SO_NBIO				0x1009

// On-Demand Nonblocking Flag
// #define INET_MSG_DONTWAIT	0x80

// TODO: Determine how many handlers we can actually have
const size_t MAX_APCTL_HANDLERS = 32;

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

typedef void(*sceNetApctlHandler)(int oldState, int newState, int event, int error, void* pArg);

#define APCTL_PROFILENAME_MAXLEN 64
#define APCTL_SSID_MAXLEN 32
#define APCTL_IPADDR_MAXLEN 16
#define APCTL_URL_MAXLEN 128
typedef struct SceNetApctlInfoInternal { // Using struct instead of union for internal use
	char 			name[APCTL_PROFILENAME_MAXLEN];
	u8 				bssid[ETHER_ADDR_LEN];
	char 			ssid[APCTL_SSID_MAXLEN];
	unsigned int 	ssidLength; // ssid string length (excluding null terminator)
	unsigned int 	securityType; // a value of PSP_NET_APCTL_INFO_SECURITY_TYPE_NONE..PSP_NET_APCTL_INFO_SECURITY_TYPE_WPA?
	u8			 	strength; // Signal strength in %
	u8			 	channel;
	u8			 	powerSave; // 1 on, 0 off
	char 			ip[APCTL_IPADDR_MAXLEN]; // PSP's IP
	char 			subNetMask[APCTL_IPADDR_MAXLEN];
	char 			gateway[APCTL_IPADDR_MAXLEN];
	char 			primaryDns[APCTL_IPADDR_MAXLEN];
	char 			secondaryDns[APCTL_IPADDR_MAXLEN];
	unsigned int 	useProxy; // 1 for proxy, 0 for no proxy
	char 			proxyUrl[APCTL_URL_MAXLEN];
	unsigned short 	proxyPort;
	unsigned int 	eapType; // 0 is none, 1 is EAP-MD5
	unsigned int 	startBrowser; // 1 = start browser
	unsigned int 	wifisp; // 1 if connection is for Wifi service providers (WISP) for sharing internet connection
} SceNetApctlInfoInternal;

struct ApctlHandler {
	u32 entryPoint;
	u32 argument;
};

struct ApctlArgs {
	u32_le data[5]; // OldState, NewState, Event, Error, ArgsAddr
};

class PointerWrap;

class AfterApctlMipsCall : public PSPAction {
public:
	AfterApctlMipsCall() {}
	static PSPAction* Create() { return new AfterApctlMipsCall(); }
	void DoState(PointerWrap& p) override;
	void run(MipsCall& call) override;
	void SetData(int HandlerID, int OldState, int NewState, int Event, int Error, u32_le ArgsAddr);

private:
	int handlerID = -1;
	int oldState = 0;
	int newState = 0;
	int event = 0;
	int error = 0;
	u32_le argsAddr = 0;
};

extern bool netInited;
extern bool netApctlInited;
extern u32 netApctlState;
extern SceNetApctlInfoInternal netApctlInfo;
extern std::string defaultNetConfigName;
extern std::string defaultNetSSID;

void Register_sceNet();
void Register_sceNetApctl();
void Register_sceWlanDrv();
void Register_sceNetUpnp();
void Register_sceNetIfhandle();


void __NetInit();
void __NetShutdown();
void __NetDoState(PointerWrap &p);

int NetApctl_GetState();

int sceNetApctlConnect(int connIndex);
int sceNetInetPoll(void *fds, u32 nfds, int timeout);
int sceNetApctlTerm();
