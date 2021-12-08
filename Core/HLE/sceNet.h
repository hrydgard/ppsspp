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

#include <StringUtils.h>
#include "Core/HLE/proAdhoc.h"

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

// Infrastructure Errno Numbers
#define INET_EAGAIN			0x0B
#define INET_ETIMEDOUT		0x74
#define INET_EINPROGRESS	0x77
#define INET_EISCONN		0x7F

// On-Demand Nonblocking Flag
#define INET_MSG_DONTWAIT	0x80

// Event Flags
#define INET_POLLRDNORM		0x0040
#define INET_POLLWRNORM		0x0004

// TODO: Determine how many handlers we can actually have
const size_t MAX_APCTL_HANDLERS = 32;

enum {
	// net common errors
	ERROR_NET_NO_SPACE						= 0x80410001,
	ERROR_NET_INTERNAL						= 0x80410002,
	ERROR_NET_INVALID_ARG					= 0x80410003,
	ERROR_NET_NO_ENTRY						= 0x80410004,

	// pspnet_core
	ERROR_NET_CORE_NOT_TERMINATED			= 0x80410101,
	ERROR_NET_CORE_INTERFACE_BUSY			= 0x80410102,
	ERROR_NET_CORE_INVALID_ARG				= 0x80410103,
	ERROR_NET_CORE_THREAD_NOT_FOUND			= 0x80410104,
	ERROR_NET_CORE_THREAD_BUSY				= 0x80410105,
	ERROR_NET_CORE_80211_NO_BSS				= 0x80410106,
	ERROR_NET_CORE_80211_NO_AVAIL_BSS		= 0x80410107,

	// pspnet_inet
	ERROR_NET_INET_ALREADY_INITIALIZED		= 0x80410201,
	ERROR_NET_INET_SOCKET_BUSY				= 0x80410202,
	ERROR_NET_INET_CONFIG_INVALID_ARG		= 0x80410203,
	ERROR_NET_INET_GET_IFADDR				= 0x80410204,
	ERROR_NET_INET_SET_IFADDR				= 0x80410205,
	ERROR_NET_INET_DEL_IFADDR				= 0x80410206,
	ERROR_NET_INET_NO_DEFAULT_ROUTE			= 0x80410207,
	ERROR_NET_INET_GET_ROUTE				= 0x80410208,
	ERROR_NET_INET_SET_ROUTE				= 0x80410209,
	ERROR_NET_INET_FLUSH_ROUTE				= 0x8041020a,
	ERROR_NET_INET_INVALID_ARG				= 0x8041020b,

	// pspnet_poeclient
	ERROR_NET_POECLIENT_INIT				= 0x80410301,
	ERROR_NET_POECLIENT_NO_PADO				= 0x80410302,
	ERROR_NET_POECLIENT_NO_PADS				= 0x80410303,
	ERROR_NET_POECLIENT_GET_PADT			= 0x80410304,
	ERROR_NET_POECLIENT_SERVICE_NAME		= 0x80410305,
	ERROR_NET_POECLIENT_AC_SYSTEM			= 0x80410306,
	ERROR_NET_POECLIENT_GENERIC				= 0x80410307,
	ERROR_NET_POECLIENT_AUTH				= 0x80410308,
	ERROR_NET_POECLIENT_NETWORK				= 0x80410309,
	ERROR_NET_POECLIENT_TERMINATE			= 0x8041030a,
	ERROR_NET_POECLIENT_NOT_STARTED			= 0x8041030b,

	// pspnet_resolver
	ERROR_NET_RESOLVER_NOT_TERMINATED		= 0x80410401,
	ERROR_NET_RESOLVER_NO_DNS_SERVER		= 0x80410402,
	ERROR_NET_RESOLVER_INVALID_PTR			= 0x80410403,
	ERROR_NET_RESOLVER_INVALID_BUFLEN		= 0x80410404,
	ERROR_NET_RESOLVER_INVALID_ID			= 0x80410405,
	ERROR_NET_RESOLVER_ID_MAX				= 0x80410406,
	ERROR_NET_RESOLVER_NO_MEM				= 0x80410407,
	ERROR_NET_RESOLVER_BAD_ID				= 0x80410408, // ERROR_NET_RESOLVER_ID_NOT_FOUND
	ERROR_NET_RESOLVER_CTX_BUSY				= 0x80410409,
	ERROR_NET_RESOLVER_ALREADY_STOPPED		= 0x8041040a,
	ERROR_NET_RESOLVER_NOT_SUPPORTED		= 0x8041040b,
	ERROR_NET_RESOLVER_BUF_NO_SPACE			= 0x8041040c,
	ERROR_NET_RESOLVER_INVALID_PACKET		= 0x8041040d,
	ERROR_NET_RESOLVER_STOPPED				= 0x8041040e,
	ERROR_NET_RESOLVER_SOCKET				= 0x8041040f,
	ERROR_NET_RESOLVER_TIMEOUT				= 0x80410410,
	ERROR_NET_RESOLVER_NO_RECORD			= 0x80410411,
	ERROR_NET_RESOLVER_RES_PACKET_FORMAT	= 0x80410412,
	ERROR_NET_RESOLVER_RES_SERVER_FAILURE	= 0x80410413,
	ERROR_NET_RESOLVER_INVALID_HOST			= 0x80410414, // ERROR_NET_RESOLVER_NO_HOST
	ERROR_NET_RESOLVER_RES_NOT_IMPLEMENTED	= 0x80410415,
	ERROR_NET_RESOLVER_RES_SERVER_REFUSED	= 0x80410416,
	ERROR_NET_RESOLVER_INTERNAL				= 0x80410417,

	// pspnet_dhcp
	ERROR_NET_DHCP_INVALID_PACKET			= 0x80410501,
	ERROR_NET_DHCP_NO_SERVER				= 0x80410502,
	ERROR_NET_DHCP_SENT_DECLINE				= 0x80410503,
	ERROR_NET_DHCP_LEASE_TIME				= 0x80410504,
	ERROR_NET_DHCP_GET_NAK		 			= 0x80410505,

	// pspnet_apctl
	ERROR_NET_APCTL_ALREADY_INITIALIZED		= 0x80410a01,
	ERROR_NET_APCTL_INVALID_CODE			= 0x80410a02,
	ERROR_NET_APCTL_INVALID_IP				= 0x80410a03,
	ERROR_NET_APCTL_NOT_DISCONNECTED		= 0x80410a04,
	ERROR_NET_APCTL_NOT_IN_BSS				= 0x80410a05,
	ERROR_NET_APCTL_WLAN_SWITCH_OFF			= 0x80410a06,
	ERROR_NET_APCTL_WLAN_BEACON_LOST		= 0x80410a07,
	ERROR_NET_APCTL_WLAN_DISASSOCIATION		= 0x80410a08,
	ERROR_NET_APCTL_INVALID_ID				= 0x80410a09,
	ERROR_NET_APCTL_WLAN_SUSPENDED  		= 0x80410a0a,
	ERROR_NET_APCTL_TIMEOUT					= 0x80410a0b,

	// wlan errors
	ERROR_NET_WLAN_ALREADY_JOINED			= 0x80410d01,
	ERROR_NET_WLAN_TRY_JOIN					= 0x80410d02,
	ERROR_NET_WLAN_SCANNING					= 0x80410d03,
	ERROR_NET_WLAN_INVALID_PARAMETER		= 0x80410d04,
	ERROR_NET_WLAN_NOT_SUPPORTED			= 0x80410d05,
	ERROR_NET_WLAN_NOT_JOIN_BSS				= 0x80410d06,
	ERROR_NET_WLAN_ASSOC_TIMEOUT			= 0x80410d07,
	ERROR_NET_WLAN_ASSOC_REFUSED			= 0x80410d08,
	ERROR_NET_WLAN_ASSOC_FAIL				= 0x80410d09,
	ERROR_NET_WLAN_DISASSOC_FAIL			= 0x80410d0a,
	ERROR_NET_WLAN_JOIN_FAIL				= 0x80410d0b,
	ERROR_NET_WLAN_POWER_OFF				= 0x80410d0c,
	ERROR_NET_WLAN_INTERNAL_FAIL			= 0x80410d0d,
	ERROR_NET_WLAN_DEVICE_NOT_READY			= 0x80410d0e,
	ERROR_NET_WLAN_ALREADY_ATTACHED			= 0x80410d0f,
	ERROR_NET_WLAN_NOT_SET_WEP				= 0x80410d10,
	ERROR_NET_WLAN_TIMEOUT					= 0x80410d11,
	ERROR_NET_WLAN_NO_SPACE					= 0x80410d12,
	ERROR_NET_WLAN_INVALID_ARG				= 0x80410D13,
	ERROR_NET_WLAN_NOT_IN_GAMEMODE			= 0x80410d14,
	ERROR_NET_WLAN_LEAVE_FAIL				= 0x80410d15,
	ERROR_NET_WLAN_SUSPENDED				= 0x80410d16,
};

enum {
	PSP_NET_APCTL_STATE_DISCONNECTED = 0,
	PSP_NET_APCTL_STATE_SCANNING = 1,
	PSP_NET_APCTL_STATE_JOINING = 2,
	PSP_NET_APCTL_STATE_GETTING_IP = 3,
	PSP_NET_APCTL_STATE_GOT_IP = 4,
	PSP_NET_APCTL_STATE_EAP_AUTH = 5,
	PSP_NET_APCTL_STATE_KEY_EXCHANGE = 6
};

enum {
	PSP_NET_APCTL_EVENT_CONNECT_REQUEST		= 0,
	PSP_NET_APCTL_EVENT_SCAN_REQUEST		= 1,
	PSP_NET_APCTL_EVENT_SCAN_COMPLETE		= 2,
	PSP_NET_APCTL_EVENT_ESTABLISHED			= 3,
	PSP_NET_APCTL_EVENT_GET_IP				= 4,
	PSP_NET_APCTL_EVENT_DISCONNECT_REQUEST	= 5,
	PSP_NET_APCTL_EVENT_ERROR				= 6,
	PSP_NET_APCTL_EVENT_INFO				= 7,
	PSP_NET_APCTL_EVENT_EAP_AUTH			= 8,
	PSP_NET_APCTL_EVENT_KEY_EXCHANGE		= 9,
	PSP_NET_APCTL_EVENT_RECONNECT			= 10,
	PSP_NET_APCTL_EVENT_SCAN_STOP			= 11 // FIXME: not sure what this is, MGS:PW seems to check this value within ApctlHandler during Recruit, related to sceNetApctlScanSSID2 ?
};

#define 	PSP_NET_APCTL_INFO_PROFILE_NAME			0
#define 	PSP_NET_APCTL_INFO_BSSID				1
#define 	PSP_NET_APCTL_INFO_SSID					2
#define 	PSP_NET_APCTL_INFO_SSID_LENGTH			3
#define 	PSP_NET_APCTL_INFO_SECURITY_TYPE		4
#define 	PSP_NET_APCTL_INFO_STRENGTH				5
#define 	PSP_NET_APCTL_INFO_CHANNEL				6
#define 	PSP_NET_APCTL_INFO_POWER_SAVE			7
#define 	PSP_NET_APCTL_INFO_IP					8
#define 	PSP_NET_APCTL_INFO_SUBNETMASK			9
#define 	PSP_NET_APCTL_INFO_GATEWAY				10
#define 	PSP_NET_APCTL_INFO_PRIMDNS				11
#define 	PSP_NET_APCTL_INFO_SECDNS				12
#define 	PSP_NET_APCTL_INFO_USE_PROXY			13
#define 	PSP_NET_APCTL_INFO_PROXY_URL			14
#define 	PSP_NET_APCTL_INFO_PROXY_PORT			15
#define 	PSP_NET_APCTL_INFO_8021_EAP_TYPE		16
#define 	PSP_NET_APCTL_INFO_START_BROWSER		17
#define 	PSP_NET_APCTL_INFO_WIFISP				18

#define 	PSP_NET_APCTL_INFO_SECURITY_TYPE_NONE           0
#define 	PSP_NET_APCTL_INFO_SECURITY_TYPE_WEP            1
#define 	PSP_NET_APCTL_INFO_SECURITY_TYPE_WPA            2
#define 	PSP_NET_APCTL_INFO_SECURITY_TYPE_UNSUPPORTED    3
#define 	PSP_NET_APCTL_INFO_SECURITY_TYPE_WPA_AES        4

#define 	PSP_NET_APCTL_DESC_IBSS                 0
#define 	PSP_NET_APCTL_DESC_SSID_NAME            1
#define 	PSP_NET_APCTL_DESC_SSID_NAME_LENGTH     2
#define 	PSP_NET_APCTL_DESC_CHANNEL              3 // FIXME: not sure what this 3 is, may be Security Type based on the order of SceNetApctlInfoInternal ?
#define 	PSP_NET_APCTL_DESC_SIGNAL_STRENGTH      4
#define 	PSP_NET_APCTL_DESC_SECURITY             5

#ifdef _MSC_VER
#pragma pack(push,1)
#endif
// Sockaddr
typedef struct SceNetInetSockaddr {
	uint8_t sa_len;
	uint8_t sa_family;
	uint8_t sa_data[14];
} PACK SceNetInetSockaddr;

// Sockaddr_in
typedef struct SceNetInetSockaddrIn {
	uint8_t sin_len;
	uint8_t sin_family;
	u16_le sin_port; //uint16_t
	u32_le sin_addr; //uint32_t
	uint8_t sin_zero[8];
} PACK SceNetInetSockaddrIn;

// Polling Event Field
typedef struct SceNetInetPollfd { //similar format to pollfd in 32bit (pollfd in 64bit have different size)
	s32_le fd;
	s16_le events;
	s16_le revents;
} PACK SceNetInetPollfd;

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
extern bool netInetInited;
extern bool netApctlInited;
extern u32 netApctlState;
extern SceNetApctlInfoInternal netApctlInfo;

template <typename I> std::string num2hex(I w, size_t hex_len = sizeof(I) << 1);
std::string error2str(u32 errorcode);

void Register_sceNet();
void Register_sceWlanDrv();
void Register_sceNetUpnp();
void Register_sceNetIfhandle();


void __NetInit();
void __NetShutdown();
void __NetDoState(PointerWrap &p);

int NetApctl_GetState();

int sceNetApctlConnect(int connIndex);
int sceNetInetPoll(void *fds, u32 nfds, int timeout);
int sceNetInetTerm();
int sceNetApctlTerm();
