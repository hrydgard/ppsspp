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

// Net stuff
#if defined(_WIN32)
#include <WS2tcpip.h>
#else
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#endif

#ifdef _MSC_VER
#define PACK  // on MSVC we use #pragma pack() instead so let's kill this.
#else
#define PACK __attribute__((packed))
#endif

#include <atomic>
#include <climits>
#include <mutex>
#include <thread>

#include "Common/Net/Resolve.h"
#include "Common/Serialize/Serializer.h"

#include "Core/CoreTiming.h"
#include "Core/MemMap.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/HLEHelperThread.h"
#include "Core/HLE/sceKernelThread.h"
#include "Core/HLE/sceKernel.h"
#include "Core/HLE/sceKernelMutex.h"
#include "Core/HLE/sceUtility.h"

#ifdef _WIN32
#undef errno
#undef ESHUTDOWN
#undef ECONNABORTED
#undef ECONNRESET
#undef ECONNREFUSED
#undef ENETUNREACH
#undef ENOTCONN
#undef EBADF
#undef EAGAIN
#undef EINPROGRESS
#undef EISCONN
#undef EALREADY
#undef ETIMEDOUT
#undef EOPNOTSUPP
#define errno WSAGetLastError()
#define ESHUTDOWN WSAESHUTDOWN
#define ECONNABORTED WSAECONNABORTED
#define ECONNRESET WSAECONNRESET
#define ECONNREFUSED WSAECONNREFUSED
#define ENETUNREACH WSAENETUNREACH
#define ENOTCONN WSAENOTCONN
#define EBADF WSAEBADF
#define EAGAIN WSAEWOULDBLOCK
#define EINPROGRESS WSAEWOULDBLOCK
#define EISCONN WSAEISCONN
#define EALREADY WSAEALREADY
#define ETIMEDOUT WSAETIMEDOUT
#define EOPNOTSUPP WSAEOPNOTSUPP
inline bool connectInProgress(int errcode){ return (errcode == WSAEWOULDBLOCK || errcode == WSAEINPROGRESS || errcode == WSAEALREADY); }
inline bool isDisconnected(int errcode) { return (errcode == WSAECONNRESET || errcode == WSAECONNABORTED || errcode == WSAESHUTDOWN); }
#else
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#define closesocket close
#ifndef ESHUTDOWN
#define ESHUTDOWN ENETDOWN
#endif
inline bool connectInProgress(int errcode){ return (errcode == EAGAIN || errcode == EWOULDBLOCK || errcode == EINPROGRESS || errcode == EALREADY); }
inline bool isDisconnected(int errcode) { return (errcode == EPIPE || errcode == ECONNRESET || errcode == ECONNABORTED || errcode == ESHUTDOWN); }
#endif

#ifndef POLL_ERR
#define POLL_ERR 0x008 /* Error condition. */
#endif
#ifndef POLLERR
#define POLLERR POLL_ERR
#endif

#ifndef POLL_PRI
#define POLL_PRI 0x002 /* There is urgent data to read. */
#endif
#ifndef POLLPRI
#define POLLPRI POLL_PRI
#endif

#ifndef SD_BOTH
#define SD_BOTH SHUT_RDWR //0x02
#endif

#define IsMatch(buf1, buf2)	(memcmp(&buf1, &buf2, sizeof(buf1)) == 0)

// Server Listening Port
#define SERVER_PORT 27312

// Default GameMode definitions
#define ADHOC_GAMEMODE_PORT 31000
#define GAMEMODE_UPDATE_INTERVAL 500 // 12000 usec on JPCSP, but lower value works better on BattleZone (in order to get full speed 60 FPS)
#define GAMEMODE_INIT_DELAY 10000
#define GAMEMODE_SYNC_TIMEOUT 250000
#define GAMEMODE_WAITID 0x2001 // Just to differentiate WaitID with other ID on WAITTYPE_NET

// GameMode Type
#define ADHOCCTL_GAMETYPE_1A	1
#define ADHOCCTL_GAMETYPE_1B	2
#define ADHOCCTL_GAMETYPE_2A	3

// psp strutcs and definitions
#define ADHOCCTL_MODE_NONE     -1 // We only use this internally as initial value before attempting to create/connect/join/scan any group
#define ADHOCCTL_MODE_NORMAL    0 // ADHOCCTL_MODE_ADHOC
#define ADHOCCTL_MODE_GAMEMODE  1

// Event Types for Event Handler
#define ADHOCCTL_EVENT_ERROR 0 // Used to pass error code to Adhocctl Handler?
#define ADHOCCTL_EVENT_CONNECT 1
#define ADHOCCTL_EVENT_DISCONNECT 2
#define ADHOCCTL_EVENT_SCAN 3
#define ADHOCCTL_EVENT_GAME 4
#define ADHOCCTL_EVENT_DISCOVER 5
#define ADHOCCTL_EVENT_WOL 6
#define ADHOCCTL_EVENT_WOL_INTERRUPT 7

// Internal Thread States
#define ADHOCCTL_STATE_DISCONNECTED 0
#define ADHOCCTL_STATE_CONNECTED 1
#define ADHOCCTL_STATE_SCANNING 2
#define ADHOCCTL_STATE_GAMEMODE 3
#define ADHOCCTL_STATE_DISCOVER 4
#define ADHOCCTL_STATE_WOL 5

// ProductType ( extracted from SSID along with ProductId & GroupName, Pattern = "PSP_([AXS])(.........)_([LG])_(.*)" )
#define PSP_ADHOCCTL_TYPE_COMMERCIAL 0
#define PSP_ADHOCCTL_TYPE_DEBUG 1
#define PSP_ADHOCCTL_TYPE_SYSTEM 2 // Used for GameSharing?

// Kernel Utility Netconf Adhoc Types
#define UTILITY_NETCONF_TYPE_CONNECT_ADHOC 2
#define UTILITY_NETCONF_TYPE_CREATE_ADHOC 4
#define UTILITY_NETCONF_TYPE_JOIN_ADHOC 5

// Kernel Utility States
#define UTILITY_NETCONF_STATUS_NONE 0
#define UTILITY_NETCONF_STATUS_INITIALIZE 1
#define UTILITY_NETCONF_STATUS_RUNNING 2
#define UTILITY_NETCONF_STATUS_FINISHED 3
#define UTILITY_NETCONF_STATUS_SHUTDOWN 4

// Event Flags
#define ADHOC_EV_SEND		0x0001
#define ADHOC_EV_RECV		0x0002
#define ADHOC_EV_CONNECT	0x0004
#define ADHOC_EV_ACCEPT		0x0008
#define ADHOC_EV_FLUSH		0x0010
#define ADHOC_EV_INVALID	0x0100 // ignored on events but can be raised on revents? similar to POLLNVAL on posix poll?
#define ADHOC_EV_DELETE		0x0200 // ignored on events but can be raised on revents? similar to POLLERR on posix poll?
#define ADHOC_EV_ALERT		0x0400
#define ADHOC_EV_DISCONNECT	0x0800 // ignored on events but can be raised on revents? similar to POLLHUP on posix poll?

// PTP Connection States
#define ADHOC_PTP_STATE_CLOSED		0
#define ADHOC_PTP_STATE_LISTEN		1
#define ADHOC_PTP_STATE_SYN_SENT	2 // 3-way handshake normally: [client]send SYN -> [server]recv SYN and reply with ACK+SYN -> [client]recv SYN and reply with ACK -> Established
#define ADHOC_PTP_STATE_SYN_RCVD	3
#define ADHOC_PTP_STATE_ESTABLISHED 4

// Nonblocking Flag for Adhoc socket API
#define ADHOC_F_NONBLOCK		0x0001
// Alert Flags
#define ADHOC_F_ALERTSEND		0x0010
#define ADHOC_F_ALERTRECV		0x0020
#define ADHOC_F_ALERTPOLL		0x0040
#define ADHOC_F_ALERTCONNECT	0x0080
#define ADHOC_F_ALERTACCEPT		0x0100
#define ADHOC_F_ALERTFLUSH		0x0200
#define ADHOC_F_ALERTALL		(ADHOC_F_ALERTSEND | ADHOC_F_ALERTRECV | ADHOC_F_ALERTPOLL | ADHOC_F_ALERTCONNECT | ADHOC_F_ALERTACCEPT | ADHOC_F_ALERTFLUSH)

/* PDP Maximum Fragment Size */
#define PSP_ADHOC_PDP_MFS		1444

/* PDP Maximum Transfer Unit */
#define PSP_ADHOC_PDP_MTU		65523

/* PTP Maximum Segment Size */
#define PSP_ADHOC_PTP_MSS		1444

/* GameMode Optional Data */
#define ADHOC_GAMEMODE_F_UPDATE		0x00000001

// Timeouts
#define PSP_ADHOCCTL_RECV_TIMEOUT	100000
#define PSP_ADHOCCTL_PING_TIMEOUT	2000000

#ifdef _MSC_VER 
#pragma pack(push, 1)
#endif
// Ethernet Address
#define ETHER_ADDR_LEN 6
typedef struct SceNetEtherAddr {
  uint8_t data[ETHER_ADDR_LEN];
} PACK SceNetEtherAddr;

inline bool operator<(const SceNetEtherAddr& lhs, const SceNetEtherAddr& rhs) {
	uint64_t l = 0;
	uint64_t r = 0;
	const uint8_t* lp = lhs.data;
	const uint8_t* rp = rhs.data;
	for (int8_t i = 5; i >= 0; i--) {
		int8_t sb = (CHAR_BIT * i);
		l |= (uint64_t)*lp++ << sb;
		r |= (uint64_t)*rp++ << sb;
	}
	return (l < r);
}

// Broadcast MAC
extern uint8_t broadcastMAC[ETHER_ADDR_LEN];

// Malloc Pool Information
typedef struct SceNetMallocStat {
	s32_le pool; // On Vantage Master Portable this is 0x1ffe0 on sceNetGetMallocStat, while the poolSize arg on sceNetInit was 0x20000
	s32_le maximum; // On Vantage Master Portable this is 0x4050, Footprint of Highest amount allocated so far?
	s32_le free; // On Vantage Master Portable this is 0x1f300, VMP compares this value with required size before sending data
} PACK SceNetMallocStat;

// Adhoc Virtual Network Name
#define ADHOCCTL_GROUPNAME_LEN 8
typedef struct SceNetAdhocctlGroupName {
  uint8_t data[ADHOCCTL_GROUPNAME_LEN];
} PACK SceNetAdhocctlGroupName;

// Virtual Network Host Information
typedef struct SceNetAdhocctlBSSId {
  SceNetEtherAddr mac_addr;
  uint8_t padding[2];
} PACK SceNetAdhocctlBSSId;

// Virtual Network Information
typedef struct SceNetAdhocctlScanInfo {
  struct SceNetAdhocctlScanInfo * next;
  s32_le channel;
  SceNetAdhocctlGroupName group_name;
  SceNetAdhocctlBSSId bssid;
  s32_le mode;
} PACK SceNetAdhocctlScanInfo;

// Virtual Network Information with u32 pointers
typedef struct SceNetAdhocctlScanInfoEmu {
	u32_le next;
	s32_le channel;
	SceNetAdhocctlGroupName group_name;
	SceNetAdhocctlBSSId bssid;
	s32_le mode;
} PACK SceNetAdhocctlScanInfoEmu;

// Player Nickname
#define ADHOCCTL_NICKNAME_LEN 128
typedef struct SceNetAdhocctlNickname {
  uint8_t data[ADHOCCTL_NICKNAME_LEN];
} PACK SceNetAdhocctlNickname;

// Active Virtual Network Information (Adhoc Group Host/Creator's device info, similar to AP?)
typedef struct SceNetAdhocctlParameter {
  s32_le channel;
  SceNetAdhocctlGroupName group_name; // This group name is probably similar to SSID name on AP
  SceNetAdhocctlNickname nickname; // According to the old PSPSDK this is the bssid, but according to the dumped content when using newer firmware this is the nickname (this is also the nickname on VitaSDK)
  SceNetAdhocctlBSSId bssid; // FIXME: bssid and nickname position might be swapped on older/newer firmware?
} PACK SceNetAdhocctlParameter;

// Peer Information (internal use only)
typedef struct SceNetAdhocctlPeerInfo {
  SceNetAdhocctlPeerInfo * next;
  SceNetAdhocctlNickname nickname;
  SceNetEtherAddr mac_addr;
  u16_le padding; // a copy of the padding(?) from SceNetAdhocctlPeerInfoEmu
  u32_le flags;
  u64_le last_recv; // Need to use the same method with sceKernelGetSystemTimeWide (ie. CoreTiming::GetGlobalTimeUsScaled) to prevent timing issue (ie. in game timeout)
  
  u32_le ip_addr; // internal use only
  u16_le port_offset; // IP-specific port offset (internal use only)
} PACK SceNetAdhocctlPeerInfo;

// Peer Information with u32 pointers
typedef struct SceNetAdhocctlPeerInfoEmu {
  u32_le next; // Changed the pointer to u32
  SceNetAdhocctlNickname nickname;
  SceNetEtherAddr mac_addr;
  u16_le padding; //00 00 // Note: Not sure whether this is really padding or reserved/unknown field
  u32_le flags; //00 04 00 00 on KHBBS and FF FF FF FF on Ys vs. Sora no Kiseki // State of the peer? Or related to sceNetAdhocAuth_CF4D9BED ?
  u64_le last_recv; // Need to use the same method with sceKernelGetSystemTimeWide (ie. CoreTiming::GetGlobalTimeUsScaled) to prevent timing issue (ie. in game timeout)
} PACK SceNetAdhocctlPeerInfoEmu;

// Member Information
typedef struct SceNetAdhocMatchingMemberInfo {
	SceNetAdhocMatchingMemberInfo * next;
	SceNetEtherAddr mac_addr;
	uint8_t padding[2];
} PACK SceNetAdhocctlMemberInfo;

// Member Information with u32 pointers
typedef struct SceNetAdhocMatchingMemberInfoEmu {
	u32_le next; // Changed the pointer to u32
	SceNetEtherAddr mac_addr;
	uint8_t padding[2];
} PACK SceNetAdhocctlMemberInfoEmu;

// Game Mode Peer List
#define ADHOCCTL_GAMEMODE_MAX_MEMBERS 16
typedef struct SceNetAdhocctlGameModeInfo {
  s32_le num;
  SceNetEtherAddr members[ADHOCCTL_GAMEMODE_MAX_MEMBERS];
} PACK SceNetAdhocctlGameModeInfo;

// GameModeUpdateInfo
typedef struct GameModeUpdateInfo {
	u32_le length; //size of GameModeUpdateInfo (16 bytes)
	s32_le updated;
	u64_le timeStamp;
} PACK GameModeUpdateInfo;

// GameModeArea (Internal use only)
typedef struct GameModeArea {
	int id; // started from 1 for replica? master = 0 or -1?
	int size;
	u32 addr;
	//int socket; // PDP socket?
	u64 updateTimestamp;
	int dataUpdated;
	int dataSent;
	SceNetEtherAddr mac;
	u8* data;  // upto "size" bytes started from "addr" ?
} PACK GameModeArea;

// Socket Polling Event Listener
typedef struct SceNetAdhocPollSd{
  s32_le id;
  s32_le events;
  s32_le revents;
} PACK SceNetAdhocPollSd;

// PDP Socket Status
typedef struct SceNetAdhocPdpStat {
	u32_le next; 
	s32_le id; // posix socket id
	SceNetEtherAddr laddr;
	u16_le lport;
	u32_le rcv_sb_cc; // Obscure The Aftermath will check if this is 0 or not before calling PdpRecv, Might to be number of bytes available to be Received?
} PACK SceNetAdhocPdpStat;

// PTP Socket Status
typedef struct SceNetAdhocPtpStat {
	u32_le next; // Changed the pointer to u32
	s32_le id; // posix socket id
	SceNetEtherAddr laddr;
	SceNetEtherAddr paddr;
	u16_le lport;
	u16_le pport;
	u32_le snd_sb_cc; // Number of bytes existed in sendBuffer to be sent/flushed
	u32_le rcv_sb_cc; // Number of bytes available in recvBuffer to be received
	s32_le state;
} PACK SceNetAdhocPtpStat;

// PDP & PTP Socket Union (Internal use only)
typedef struct AdhocSocket {
	s32_le type; // SOCK_PDP/SOCK_PTP
	s32_le flags; // Socket Alert Flags
	s32_le alerted_flags; // Socket Alerted Flags
	s32_le nonblocking; // last non-blocking flag
	u32 buffer_size;
	u32 send_timeout; // default connect timeout
	u32 recv_timeout; // default accept timeout
	s32 retry_interval; // related to keepalive
	s32 retry_count; // multiply with retry interval to be used as keepalive timeout
	s32 attemptCount; // connect/accept attempts
	u64 lastAttempt; // timestamp to retry again (attempted by the game)
	u64 internalLastAttempt; // timestamp to retry again (internal use only)
	bool isClient; // true if the game is using local port 0 when creating the socket
	union {
		SceNetAdhocPdpStat pdp;
		SceNetAdhocPtpStat ptp;
	} data;
} PACK AdhocSocket;

// Gamemode Optional Peer Buffer Data
typedef struct SceNetAdhocGameModeOptData {
  u32_le size;
  u32_le flag;
  u64_le last_recv; // Need to use the same method with sceKernelGetSystemTimeWide (ie. CoreTiming::GetGlobalTimeUsScaled) to prevent timing issue (ie. in game timeout)
} PACK SceNetAdhocGameModeOptData;

// Gamemode Buffer Status
typedef struct SceNetAdhocGameModeBufferStat {
  struct SceNetAdhocGameModeBufferStat * next; //should be u32_le ?
  s32_le id;
  void * ptr; //should be u32_le ?
  u32_le size;
  u32_le master;
  SceNetAdhocGameModeOptData opt;
} PACK SceNetAdhocGameModeBufferStat;

// Adhoc ID (Game Product Key)
#define ADHOCCTL_ADHOCID_LEN 9
typedef struct SceNetAdhocctlAdhocId {
	s32_le type; // Air Conflicts - Aces Of World War 2 is using 2 for GameSharing?
	uint8_t data[ADHOCCTL_ADHOCID_LEN]; // Air Conflicts - Aces Of World War 2 is using "000000001" for GameSharing?
	uint8_t padding[3];
} PACK SceNetAdhocctlAdhocId; // should this be packed?
#ifdef _MSC_VER 
#pragma pack(pop)
#endif


// Internal Matching Peer Information
typedef struct SceNetAdhocMatchingMemberInternal {
  // Next Peer
  struct SceNetAdhocMatchingMemberInternal * next;

  // MAC Address
  SceNetEtherAddr mac;

  // State Variable
  s32_le state;

  // Send in Progress
  s32_le sending;

  // Last Heartbeat
  u64_le lastping; // May need to use the same method with sceKernelGetSystemTimeWide (ie. CoreTiming::GetGlobalTimeUsScaled) to prevent timing issue (ie. in game timeout)
} SceNetAdhocMatchingMemberInternal;


// Matching handler
struct SceNetAdhocMatchingHandlerArgs {
  s32_le id;
  s32_le opcode; // event;
  SceNetEtherAddr mac; // peer //u32_le macaddr;
  s32_le optlen;
  void * opt; //u32_le optaddr
};

struct SceNetAdhocMatchingHandler {
  u32_le entryPoint;
};

struct AdhocctlHandler {
	u32 entryPoint;
	u32 argument;
};

// Thread Message Stack Item
typedef struct ThreadMessage {
  // Next Thread Message
  struct ThreadMessage * next;

  // Stack Event Opcode
  u32_le opcode;

  // Target MAC Address
  SceNetEtherAddr mac;

  // Optional Data Length
  s32_le optlen;
} ThreadMessage;

// Established Peer

// Context Information
typedef struct SceNetAdhocMatchingContext {
  // Next Context
  struct SceNetAdhocMatchingContext *next;

  // Externally Visible ID
  s32_le id;

  // Matching Mode (HOST, CLIENT, P2P)
  s32_le mode;

  // Running Flag (1 = running, 0 = created)
  s32_le running;

  // Maximum Number of Peers (for HOST, P2P)
  s32_le maxpeers;

  // Peer List for Connectees
  SceNetAdhocMatchingMemberInternal *peerlist; // SceNetAdhocMatchingMemberInfo[Emu]

  // Peer Port list
  std::map<SceNetEtherAddr, u16_le> *peerPort;

  // Local MAC Address
  SceNetEtherAddr mac;

  // Local PDP Port
  u16_le port;

  // Local PDP Socket
  s32_le socket;
  // Socket Lock
  std::recursive_mutex *socketlock;

  // Receive Buffer Length
  s32_le rxbuflen;

  // Receive Buffer
  uint8_t *rxbuf;

  // Hello Broadcast Interval (Microseconds)
  u32_le hello_int;

  // Keep-Alive Broadcast Interval (Microseconds)
  u32_le keepalive_int;

  // Resend Interval (Microseconds)
  u32_le resend_int;

  // Resend-Counter
  s32_le resendcounter;

  // Keep-Alive Counter
  s32_le keepalivecounter;

  // Event Handler
  SceNetAdhocMatchingHandler handler;

  // Event Handler Args
  u32_le handlerArgs[6]; //MatchingArgs handlerArgs; // actual arguments only 5, the 6th one is just for borrowing a space to store the callback address to use later
  //SceNetAdhocMatchingHandlerArgs handlerArgs;

  // Hello Data Length
  s32_le hellolen;

  // Hello Data Address
  u32_le helloAddr;

  // Hello Data
  uint8_t *hello;

  // Timeout
  u64_le timeout;

  // Helper Thread (fake PSP Thread) needed to execute callback
  //HLEHelperThread *matchingThread;
  int matching_thid;

  // Event Caller Thread
  std::thread eventThread;
  //s32_le event_thid;
  bool eventRunning = false;
  bool IsMatchingInCB = false;

  // IO Handler Thread
  std::thread inputThread;
  //s32_le input_thid;
  bool inputRunning = false;

  // Event Caller Thread Message Stack
  std::recursive_mutex *eventlock; // s32_le event_stack_lock;
  ThreadMessage *event_stack;

  // IO Handler Thread Message Stack
  std::recursive_mutex *inputlock; // s32_le input_stack_lock;
  ThreadMessage *input_stack;

  // Socket Connectivity
  //bool connected = false;
  //bool InConnection = false;
  //u32_le handlerid = -1;
  //int eventMatchingHandlerUpdate = -1;
} SceNetAdhocMatchingContext;

// End of psp definitions

enum {
	// pspnet_adhoc_auth
	ERROR_NET_ADHOC_AUTH_ALREADY_INITIALIZED		= 0x80410601,

	// pspnet_adhoc
	ERROR_NET_ADHOC_INVALID_SOCKET_ID				= 0x80410701,
	ERROR_NET_ADHOC_INVALID_ADDR					= 0x80410702,
	ERROR_NET_ADHOC_INVALID_PORT					= 0x80410703,
	ERROR_NET_ADHOC_INVALID_BUFLEN					= 0x80410704,
	ERROR_NET_ADHOC_INVALID_DATALEN					= 0x80410705,
	ERROR_NET_ADHOC_NOT_ENOUGH_SPACE				= 0x80400706, // not a typo
	ERROR_NET_ADHOC_SOCKET_DELETED					= 0x80410707,
	ERROR_NET_ADHOC_SOCKET_ALERTED					= 0x80410708,
	ERROR_NET_ADHOC_WOULD_BLOCK						= 0x80410709, //ERROR_NET_ADHOC_NO_DATA_AVAILABLE
	ERROR_NET_ADHOC_PORT_IN_USE						= 0x8041070a,
	ERROR_NET_ADHOC_NOT_CONNECTED					= 0x8041070B,
	ERROR_NET_ADHOC_DISCONNECTED					= 0x8041070c,
	ERROR_NET_ADHOC_NOT_OPENED						= 0x8040070D, // not a typo
	ERROR_NET_ADHOC_NOT_LISTENED					= 0x8040070E, // not a typo
	ERROR_NET_ADHOC_SOCKET_ID_NOT_AVAIL				= 0x8041070F,
	ERROR_NET_ADHOC_PORT_NOT_AVAIL					= 0x80410710,
	ERROR_NET_ADHOC_INVALID_ARG						= 0x80410711,
	ERROR_NET_ADHOC_NOT_INITIALIZED					= 0x80410712,
	ERROR_NET_ADHOC_ALREADY_INITIALIZED				= 0x80410713,
	ERROR_NET_ADHOC_BUSY							= 0x80410714,
	ERROR_NET_ADHOC_TIMEOUT							= 0x80410715,
	ERROR_NET_ADHOC_NO_ENTRY						= 0x80410716,
	ERROR_NET_ADHOC_EXCEPTION_EVENT					= 0x80410717,
	ERROR_NET_ADHOC_CONNECTION_REFUSED				= 0x80410718,
	ERROR_NET_ADHOC_THREAD_ABORTED					= 0x80410719,
	ERROR_NET_ADHOC_ALREADY_CREATED					= 0x8041071A,
	ERROR_NET_ADHOC_NOT_IN_GAMEMODE					= 0x8041071B,
	ERROR_NET_ADHOC_NOT_CREATED						= 0x8041071C,

	// pspnet_adhoc_matching
	ERROR_NET_ADHOC_MATCHING_INVALID_MODE			= 0x80410801,
	ERROR_NET_ADHOC_MATCHING_INVALID_PORT			= 0x80410802,
	ERROR_NET_ADHOC_MATCHING_INVALID_MAXNUM			= 0x80410803,
	ERROR_NET_ADHOC_MATCHING_RXBUF_TOO_SHORT		= 0x80410804,
	ERROR_NET_ADHOC_MATCHING_INVALID_OPTLEN			= 0x80410805,
	ERROR_NET_ADHOC_MATCHING_INVALID_ARG			= 0x80410806,
	ERROR_NET_ADHOC_MATCHING_INVALID_ID				= 0x80410807,
	ERROR_NET_ADHOC_MATCHING_ID_NOT_AVAIL			= 0x80410808,
	ERROR_NET_ADHOC_MATCHING_NO_SPACE				= 0x80410809,
	ERROR_NET_ADHOC_MATCHING_IS_RUNNING				= 0x8041080A,
	ERROR_NET_ADHOC_MATCHING_NOT_RUNNING			= 0x8041080B,
	ERROR_NET_ADHOC_MATCHING_UNKNOWN_TARGET			= 0x8041080C,
	ERROR_NET_ADHOC_MATCHING_TARGET_NOT_READY		= 0x8041080D,
	ERROR_NET_ADHOC_MATCHING_EXCEED_MAXNUM			= 0x8041080E,
	ERROR_NET_ADHOC_MATCHING_REQUEST_IN_PROGRESS	= 0x8041080F,
	ERROR_NET_ADHOC_MATCHING_ALREADY_ESTABLISHED	= 0x80410810,
	ERROR_NET_ADHOC_MATCHING_BUSY					= 0x80410811,
	ERROR_NET_ADHOC_MATCHING_ALREADY_INITIALIZED	= 0x80410812,
	ERROR_NET_ADHOC_MATCHING_NOT_INITIALIZED		= 0x80410813,
	ERROR_NET_ADHOC_MATCHING_PORT_IN_USE			= 0x80410814,
	ERROR_NET_ADHOC_MATCHING_STACKSIZE_TOO_SHORT	= 0x80410815,
	ERROR_NET_ADHOC_MATCHING_INVALID_DATALEN		= 0x80410816,
	ERROR_NET_ADHOC_MATCHING_NOT_ESTABLISHED		= 0x80410817,
	ERROR_NET_ADHOC_MATCHING_DATA_BUSY				= 0x80410818,

	// pspnet_adhocctl
	ERROR_NET_ADHOCCTL_NOT_LEFT_IBSS				= 0x80410b01,
	ERROR_NET_ADHOCCTL_ALREADY_CONNECTED			= 0x80410b02,
	ERROR_NET_ADHOCCTL_WLAN_SWITCH_OFF				= 0x80410b03,
	ERROR_NET_ADHOCCTL_INVALID_ARG					= 0x80410B04,
	ERROR_NET_ADHOCCTL_TIMEOUT						= 0x80410b05,
	ERROR_NET_ADHOCCTL_ID_NOT_FOUND					= 0x80410B06,
	ERROR_NET_ADHOCCTL_ALREADY_INITIALIZED			= 0x80410b07,
	ERROR_NET_ADHOCCTL_NOT_INITIALIZED				= 0x80410b08,
	ERROR_NET_ADHOCCTL_DISCONNECTED					= 0x80410b09,
	ERROR_NET_ADHOCCTL_NO_SCAN_INFO					= 0x80410b0a,
	ERROR_NET_ADHOCCTL_INVALID_IBSS					= 0x80410b0b,
	ERROR_NET_ADHOCCTL_NOT_ENTER_GAMEMODE			= 0x80410B0C,
	ERROR_NET_ADHOCCTL_CHANNEL_NOT_AVAILABLE		= 0x80410B0D,
	ERROR_NET_ADHOCCTL_WLAN_BEACON_LOST				= 0x80410b0e,
	ERROR_NET_ADHOCCTL_WLAN_SUSPENDED				= 0x80410b0f,
	ERROR_NET_ADHOCCTL_BUSY							= 0x80410b10,
	ERROR_NET_ADHOCCTL_CHANNEL_NOT_MATCH			= 0x80410b11,
	ERROR_NET_ADHOCCTL_TOO_MANY_HANDLERS			= 0x80410b12,
	ERROR_NET_ADHOCCTL_STACKSIZE_TOO_SHORT			= 0x80410B13,
};

const size_t MAX_ADHOCCTL_HANDLERS = 32; //4
const size_t MAX_MATCHING_HANDLERS = 32; //4

enum {
	/**
	* Matching events used in pspAdhocMatchingCallback
	*/
	/** Hello event. optdata contains data if optlen > 0. */
	PSP_ADHOC_MATCHING_EVENT_HELLO = 1,
	/** Join request. optdata contains data if optlen > 0. */
	PSP_ADHOC_MATCHING_EVENT_JOIN = 2,
	/** Target left matching. */
	PSP_ADHOC_MATCHING_EVENT_LEFT = 3,
	/** Join request rejected. */
	PSP_ADHOC_MATCHING_EVENT_REJECT = 4,
	/** Join request cancelled. */
	PSP_ADHOC_MATCHING_EVENT_CANCEL = 5,
	/** Join request accepted. optdata contains data if optlen > 0. */
	PSP_ADHOC_MATCHING_EVENT_ACCEPT = 6,
	/** Matching is complete. */
	PSP_ADHOC_MATCHING_EVENT_COMPLETE = 7,
	/** Ping timeout event. */
	PSP_ADHOC_MATCHING_EVENT_TIMEOUT = 8,
	/** Error event. */
	PSP_ADHOC_MATCHING_EVENT_ERROR = 9,
	/** Peer disconnect event. */
	PSP_ADHOC_MATCHING_EVENT_DISCONNECT = 10,
	/** Data received event. optdata contains data if optlen > 0. */
	PSP_ADHOC_MATCHING_EVENT_DATA = 11,
	/** Data acknowledged event. */
	PSP_ADHOC_MATCHING_EVENT_DATA_CONFIRM = 12,
	/** Data timeout event. */
	PSP_ADHOC_MATCHING_EVENT_DATA_TIMEOUT = 13,

	/** Internal ping message. */
	PSP_ADHOC_MATCHING_EVENT_INTERNAL_PING = 100,

	/**
	* Matching modes used in sceNetAdhocMatchingCreate
	*/
	/** Host */
	PSP_ADHOC_MATCHING_MODE_HOST = 1,
	/** Client */
	PSP_ADHOC_MATCHING_MODE_CLIENT = 2,
	/** Peer to peer */
	PSP_ADHOC_MATCHING_MODE_PTP = 3,
};

enum {
	PSP_ADHOC_POLL_READY_TO_SEND = 1, // POLLIN ?
	PSP_ADHOC_POLL_DATA_AVAILABLE = 2, // POLLPRI ?
	PSP_ADHOC_POLL_CAN_CONNECT = 4, // POLLOUT ?
	PSP_ADHOC_POLL_CAN_ACCEPT = 8, // POLLERR ?
};

// Matching modes
#define PSP_ADHOC_MATCHING_MODE_PARENT			1
#define PSP_ADHOC_MATCHING_MODE_CHILD			2
#define PSP_ADHOC_MATCHING_MODE_P2P				3

// Matching Events
#define PSP_ADHOC_MATCHING_EVENT_HELLO			1	// Should be ignored when Join Request is in progress ?
#define PSP_ADHOC_MATCHING_EVENT_REQUEST		2
#define PSP_ADHOC_MATCHING_EVENT_LEAVE			3
#define PSP_ADHOC_MATCHING_EVENT_DENY			4
#define PSP_ADHOC_MATCHING_EVENT_CANCEL			5
#define PSP_ADHOC_MATCHING_EVENT_ACCEPT			6
#define PSP_ADHOC_MATCHING_EVENT_ESTABLISHED	7	// Should only be triggered on Parent/P2P mode and for Parent/P2P peer ?
#define PSP_ADHOC_MATCHING_EVENT_TIMEOUT		8	// Should only be triggered on Parent/P2P mode and for Parent/P2P peer ?
#define PSP_ADHOC_MATCHING_EVENT_ERROR			9
#define PSP_ADHOC_MATCHING_EVENT_BYE			10	// Should only be triggered on Parent/P2P mode and for Parent/P2P peer ?
#define PSP_ADHOC_MATCHING_EVENT_DATA			11
#define PSP_ADHOC_MATCHING_EVENT_DATA_ACK		12
#define PSP_ADHOC_MATCHING_EVENT_DATA_TIMEOUT	13

// Peer Status
// Offer only seen in P2P and PARENT mode after hello
// Parent only seen in CHILD mode after connection accept
// Child only seen in PARENT and CHILD mode after connection accept
// P2P only seen in P2P mode after connection accept
// Requester only seen in P2P and PARENT mode after connection request
#define PSP_ADHOC_MATCHING_PEER_OFFER				1
#define PSP_ADHOC_MATCHING_PEER_PARENT				2
#define PSP_ADHOC_MATCHING_PEER_CHILD				3
#define PSP_ADHOC_MATCHING_PEER_P2P					4
#define PSP_ADHOC_MATCHING_PEER_INCOMING_REQUEST	5
#define PSP_ADHOC_MATCHING_PEER_OUTGOING_REQUEST	6
#define PSP_ADHOC_MATCHING_PEER_CANCEL_IN_PROGRESS	7

// Stack Targets
#define PSP_ADHOC_MATCHING_INPUT_STACK	1
#define PSP_ADHOC_MATCHING_EVENT_STACK	2

// Packet Opcodes
#define PSP_ADHOC_MATCHING_PACKET_PING			0
#define PSP_ADHOC_MATCHING_PACKET_HELLO			1
#define PSP_ADHOC_MATCHING_PACKET_JOIN			2
#define PSP_ADHOC_MATCHING_PACKET_ACCEPT		3
#define PSP_ADHOC_MATCHING_PACKET_CANCEL		4
#define PSP_ADHOC_MATCHING_PACKET_BULK			5
#define PSP_ADHOC_MATCHING_PACKET_BULK_ABORT	6
#define PSP_ADHOC_MATCHING_PACKET_BIRTH			7
#define PSP_ADHOC_MATCHING_PACKET_DEATH			8
#define PSP_ADHOC_MATCHING_PACKET_BYE			9

// Pro Adhoc Server Packets Opcodes
#define OPCODE_PING 0
#define OPCODE_LOGIN 1
#define OPCODE_CONNECT 2
#define OPCODE_DISCONNECT 3
#define OPCODE_SCAN 4
#define OPCODE_SCAN_COMPLETE 5
#define OPCODE_CONNECT_BSSID 6
#define OPCODE_CHAT 7

// PSP Product Code
#define PRODUCT_CODE_LENGTH 9

#ifdef _MSC_VER 
#pragma pack(push,1) 
#endif

typedef struct {
  // Game Product Code (ex. ULUS12345)
  char data[PRODUCT_CODE_LENGTH];
} PACK SceNetAdhocctlProductCode;

// Basic Packet
typedef struct {
  uint8_t opcode;
} PACK SceNetAdhocctlPacketBase;

// C2S Login Packet
typedef struct {
  SceNetAdhocctlPacketBase base;
  SceNetEtherAddr mac;
  SceNetAdhocctlNickname name;
  SceNetAdhocctlProductCode game;
} PACK SceNetAdhocctlLoginPacketC2S;

// C2S Connect Packet
typedef struct {
  SceNetAdhocctlPacketBase base;
  SceNetAdhocctlGroupName group;
} PACK SceNetAdhocctlConnectPacketC2S;

#define ADHOCCTL_MESSAGE_LEN 64
// C2S Chat Packet
typedef struct {
  SceNetAdhocctlPacketBase base;
  char message[ADHOCCTL_MESSAGE_LEN];
} PACK SceNetAdhocctlChatPacketC2S;

// S2C Connect Packet
typedef struct {
  SceNetAdhocctlPacketBase base;
  SceNetAdhocctlNickname name;
  SceNetEtherAddr mac;
  uint32_t ip;
} PACK SceNetAdhocctlConnectPacketS2C;

// S2C Disconnect Packet
typedef struct {
  SceNetAdhocctlPacketBase base;
  uint32_t ip;
} PACK SceNetAdhocctlDisconnectPacketS2C;

// S2C Scan Packet
typedef struct {
  SceNetAdhocctlPacketBase base;
  SceNetAdhocctlGroupName group;
  SceNetEtherAddr mac;
} PACK SceNetAdhocctlScanPacketS2C;

// S2C Connect BSSID Packet
typedef struct {
  SceNetAdhocctlPacketBase base;
  SceNetEtherAddr mac;
} PACK SceNetAdhocctlConnectBSSIDPacketS2C;

// S2C Chat Packet
typedef struct {
  SceNetAdhocctlChatPacketC2S base;
  SceNetAdhocctlNickname name;
} PACK SceNetAdhocctlChatPacketS2C;

// P2P Packet
typedef struct {
	SceNetEtherAddr fromMAC;
	SceNetEtherAddr toMAC;
	u32_le dataPtr; //void * data
} PACK SceNetAdhocMatchingPacketBase;

// P2P Accept Packet
typedef struct {
	SceNetAdhocctlPacketBase base; //opcode
	u32_le dataLen;
	u32_le numMACs; //number of peers
	u32_le dataPtr; //void * data
	/*u32_le*/PSPPointer<SceNetEtherAddr> MACsPtr; //peers //SceNetEtherAddr * MACs
} PACK SceNetAdhocMatchingPacketAccept;

#ifdef _MSC_VER 
#pragma pack(pop)
#endif

class PointerWrap;

class AfterAdhocMipsCall : public PSPAction {
public:
	AfterAdhocMipsCall() {}
	static PSPAction* Create() { return new AfterAdhocMipsCall(); }
	void DoState(PointerWrap& p) override;
	void run(MipsCall& call) override;
	void SetData(int handlerID, int eventId, u32_le argsAddr);

private:
	int HandlerID = -1;
	int EventID = -1;
	u32_le argsAddr = 0;
};

class AfterMatchingMipsCall : public PSPAction {
public:
	AfterMatchingMipsCall() {}
	static PSPAction *Create() { return new AfterMatchingMipsCall(); }
	void DoState(PointerWrap &p) override;
	void run(MipsCall &call) override;
	void SetData(int ContextID, int eventId, u32_le BufAddr);

private:
	int contextID = -1;
	int EventID = -1;
	u32_le bufAddr = 0;
	SceNetAdhocMatchingContext* context = nullptr;
};

extern int actionAfterAdhocMipsCall;
extern int actionAfterMatchingMipsCall;

#define MAX_SOCKET	255 // FIXME: PSP might not allows more than 255 sockets? Hotshots Tennis doesn't seems to works with socketId > 255
#define SOCK_PDP	1
#define SOCK_PTP	2
// Aux vars
extern std::atomic<int> metasocket;
extern SceNetAdhocctlParameter parameter;
extern SceNetAdhocctlAdhocId product_code;
extern std::thread friendFinderThread;
extern std::recursive_mutex peerlock;
extern AdhocSocket* adhocSockets[MAX_SOCKET];

union SockAddrIN4 {
	sockaddr addr;
	sockaddr_in in;
};

extern uint16_t portOffset;
extern uint32_t minSocketTimeoutUS;
extern bool isOriPort;
extern bool isLocalServer;
extern SockAddrIN4 g_adhocServerIP; // Resolved Adhoc Server IP so we don't need to repeatedly resolve the DNS again later
extern SockAddrIN4 g_localhostIP; // Used to differentiate localhost IP on multiple-instance
extern sockaddr LocalIP; // IP of Network Adapter used to connect to Adhoc Server (LAN/WAN)
extern int defaultWlanChannel; // Default WLAN Channel for Auto, JPCSP uses 11

extern uint32_t fakePoolSize;
extern SceNetAdhocMatchingContext * contexts;
extern char* dummyPeekBuf64k;
extern int dummyPeekBuf64kSize;
extern int one;                 
extern std::atomic<bool> friendFinderRunning;
extern SceNetAdhocctlPeerInfo * friends;
extern SceNetAdhocctlScanInfo * networks;
extern u64 adhocctlStartTime;
extern bool isAdhocctlNeedLogin;
extern bool isAdhocctlBusy;
extern int adhocctlState;
extern int adhocctlCurrentMode;
extern int adhocConnectionType;

extern int gameModeSocket;
extern int gameModeBuffSize;
extern u8* gameModeBuffer;
extern GameModeArea masterGameModeArea;
extern std::vector<GameModeArea> replicaGameModeAreas;
extern std::vector<SceNetEtherAddr> requiredGameModeMacs;
extern std::vector<SceNetEtherAddr> gameModeMacs;
extern std::map<SceNetEtherAddr, u16_le> gameModePeerPorts;
// End of Aux vars

enum AdhocConnectionType : int
{
	ADHOC_CONNECT = 0,
	ADHOC_CREATE = 1,
	ADHOC_JOIN = 2,
};

// Check if Matching callback is running
bool IsMatchingInCallback(SceNetAdhocMatchingContext * context);
bool SetMatchingInCallback(SceNetAdhocMatchingContext* context, bool IsInCB);

int IsAdhocctlInCallback();
int SetAdhocctlInCallback(bool IsInCB);

/**
 * Compare MAC Addresses
 * @param addr1 & addr2 To-be-compared MAC Address
 * @return True if both matched
 */
bool isMacMatch(const SceNetEtherAddr* addr1, const SceNetEtherAddr* addr2);

/**
 * Local MAC Check
 * @param saddr To-be-checked MAC Address
 * @return True if it's local mac
 */
bool isLocalMAC(const SceNetEtherAddr * addr);

/**
 * PDP Port Check
 * @param port To-be-checked Port
 * @return 1 if in use or... 0
 */
bool isPDPPortInUse(uint16_t port);

/**
 * Check whether PTP Port is in use or not (only sockets with non-Listening state will be considered as in use)
 * @param port To-be-checked Port Number
 * @param forListen to check for listening or non-listening port
 * @param dstmac destination address (non-listening only)
 * @param dstport destination port (non-listening only)
 * @return 1 if in use or... 0
 */
bool isPTPPortInUse(uint16_t port, bool forListen, SceNetEtherAddr* dstmac = nullptr, uint16_t dstport = 0);

// Convert IPv4 address to string (Replacement for inet_ntoa since it's getting deprecated)
std::string ip2str(in_addr in, bool maskPublicIP = true);

// Convert MAC address to string
std::string mac2str(SceNetEtherAddr* mac);

/*
 * Matching Members
 */
//SceNetAdhocMatchingMemberInternal* findMember(SceNetAdhocMatchingContext * context, SceNetEtherAddr * mac); // findPeer
SceNetAdhocMatchingMemberInternal* addMember(SceNetAdhocMatchingContext * context, SceNetEtherAddr * mac);
//void deleteMember(SceNetAdhocMatchingContext * context, SceNetEtherAddr * mac); // deletePeer
//void deleteAllMembers(SceNetAdhocMatchingContext * context); // clearPeerList


/**
 * Add Friend to Local List
 * @param packet Friend Information
 */
void addFriend(SceNetAdhocctlConnectPacketS2C * packet);

/**
* Send chat or get that
* @param std::string ChatString 
*/
void sendChat(std::string chatString);
std::vector<std::string> getChatLog();
int GetChatChangeID();
int GetChatMessageCount();

/*
 * Find a Peer/Friend by MAC address
 */
SceNetAdhocctlPeerInfo * findFriend(SceNetEtherAddr * MAC);

/*
 * Find a Peer/Friend by IP address
 */
SceNetAdhocctlPeerInfo* findFriendByIP(uint32_t ip);

/**
 * Get the Readability(ie. recv) and/or Writability(ie. send) of a socket
 * @param fd File Descriptor of the socket
 * @param timeout in usec (micro seconds), 0 = non-blocking
 * @return > 0 = ready, 0 = timeout, -1 = error (errorcode only represent error of select and doesn't represent error of the socket)
 */
int IsSocketReady(int fd, bool readfd, bool writefd, int* errorcode = nullptr, int timeoutUS = 0);

/**
 * Changes the Blocking Mode of the socket
 * @param fd File Descriptor of the socket
 * @param nonblocking 1 to set to nonblock and 0 to set blocking
 */
void changeBlockingMode(int fd, int nonblocking);

/**
 * Count Virtual Networks by analyzing the Friend List
 * @return Number of Virtual Networks
 */
int countAvailableNetworks(const bool excludeSelf = false);

/*
 * Find an existing group in networks
 */
SceNetAdhocctlScanInfo * findGroup(SceNetEtherAddr * MAC);

/*
* Deletes all groups in networks
*/
void freeGroupsRecursive(SceNetAdhocctlScanInfo * node);

/**
 * Closes & Deletes all PDP & PTP Sockets
 */
void deleteAllAdhocSockets();

/*
* Deletes all GameMode Buffers
*/
void deleteAllGMB();

/**
 * Delete Friend from Local List
 * @param ip Friend IP
 */
void deleteFriendByIP(uint32_t ip);

/**
 * Recursive Memory Freeing-Helper for Friend-Structures
 * @param node Current Node in List
 */
void freeFriendsRecursive(SceNetAdhocctlPeerInfo * node, int32_t* count = nullptr);

void timeoutFriendsRecursive(SceNetAdhocctlPeerInfo * node, int32_t* count = nullptr);

/**
 * Friend Finder Thread (Receives Peer Information)
 * @param args Length of argp in Bytes (Unused)
 * @param argp Argument (Unused)
 * @return Unused Value - Return 0
 */
int friendFinder();

/**
* Find Free Matching ID
* @return First unoccupied Matching ID
*/
int findFreeMatchingID();

/**
* Find Internal Matching Context for Matching ID
* @param id Matching ID
* @return Matching Context Pointer or... NULL
*/
SceNetAdhocMatchingContext * findMatchingContext(int id);

/*
* Notify Matching Event Handler
*/
void notifyMatchingHandler(SceNetAdhocMatchingContext * context, ThreadMessage * msg, void * opt, u32_le &bufAddr, u32_le &bufLen, u32_le * args);
// Notifiy Adhocctl Handlers
void notifyAdhocctlHandlers(u32 flag, u32 error);

/*
 * Packet Handler
 */
void postAcceptCleanPeerList(SceNetAdhocMatchingContext * context);
void postAcceptAddSiblings(SceNetAdhocMatchingContext * context, int siblingcount, SceNetEtherAddr * siblings);

/*
 * Timeout Handler
 */
void handleTimeout(SceNetAdhocMatchingContext * context);

/**
* Clear Thread Stack
* @param context Matching Context Pointer
* @param stack ADHOC_MATCHING_EVENT_STACK or ADHOC_MATCHING_INPUT_STACK
*/
void clearStack(SceNetAdhocMatchingContext * context, int stack);

/**
* Clear Peer List
* @param context Matching Context Pointer
*/
void clearPeerList(SceNetAdhocMatchingContext * context);

/**
* Find Outgoing Request Target Peer
* @param context Matching Context Pointer
* @return Internal Peer Reference or... NULL
*/
SceNetAdhocMatchingMemberInternal * findOutgoingRequest(SceNetAdhocMatchingContext * context);

/**
* Send Accept Message from P2P -> P2P or Parent -> Children
* @param context Matching Context Pointer
* @param peer Target Peer
* @param optlen Optional Data Length
* @param opt Optional Data
*/
void sendAcceptMessage(SceNetAdhocMatchingContext * context, SceNetAdhocMatchingMemberInternal * peer, int optlen, const void * opt);

/**
* Send Join Request from P2P -> P2P or Children -> Parent
* @param context Matching Context Pointer
* @param peer Target Peer
* @param optlen Optional Data Length
* @param opt Optional Data
*/
void sendJoinRequest(SceNetAdhocMatchingContext * context, SceNetAdhocMatchingMemberInternal * peer, int optlen, const void * opt);

/**
* Send Cancel Message to Peer (has various effects)
* @param context Matching Context Pointer
* @param peer Target Peer
* @param optlen Optional Data Length
* @param opt Optional Data
*/
void sendCancelMessage(SceNetAdhocMatchingContext * context, SceNetAdhocMatchingMemberInternal * peer, int optlen, const void * opt);

/**
* Send Bulk Data to Peer
* @param context Matching Context Pointer
* @param peer Target Peer
* @param datalen Data Length
* @param data Data
*/
void sendBulkData(SceNetAdhocMatchingContext * context, SceNetAdhocMatchingMemberInternal * peer, int datalen, const void * data);

/**
* Abort Bulk Data Transfer (if in progress)
* @param context Matching Context Pointer
* @param peer Target Peer
*/
void abortBulkTransfer(SceNetAdhocMatchingContext * context, SceNetAdhocMatchingMemberInternal * peer);

/**
* Notify all established Peers about new Kid in the Neighborhood
* @param context Matching Context Pointer
* @param peer New Kid
*/
void sendBirthMessage(SceNetAdhocMatchingContext * context, SceNetAdhocMatchingMemberInternal * peer);

/**
* Notify all established Peers about abandoned Child
* @param context Matching Context Pointer
* @param peer Abandoned Child
*/
void sendDeathMessage(SceNetAdhocMatchingContext * context, SceNetAdhocMatchingMemberInternal * peer);

/**
* Count Children Peers (for Parent)
* @param context Matching Context Pointer
* @return Number of Children
*/
s32_le countChildren(SceNetAdhocMatchingContext * context, const bool excludeTimedout = false);

/**
* Delete Peer from List
* @param context Matching Context Pointer
* @param peer Internal Peer Reference
*/
void deletePeer(SceNetAdhocMatchingContext * context, SceNetAdhocMatchingMemberInternal *& peer);

/**
* Find Peer in Context by MAC
* @param context Matching Context Pointer
* @param mac Peer MAC Address
* @return Internal Peer Reference or... NULL
*/
SceNetAdhocMatchingMemberInternal * findPeer(SceNetAdhocMatchingContext * context, SceNetEtherAddr * mac);

/**
* Find Parent Peer
* @param context Matching Context Pointer
* @return Internal Peer Reference or... NULL
*/
SceNetAdhocMatchingMemberInternal * findParent(SceNetAdhocMatchingContext * context);

/**
* Find P2P Buddy Peer
* @param context Matching Context Pointer
* @return Internal Peer Reference or... NULL
*/
SceNetAdhocMatchingMemberInternal * findP2P(SceNetAdhocMatchingContext * context, const bool excludeTimedout = false);

/**
* Return Number of Connected Peers
* @param context Matching Context Pointer
* @return Number of Connected Peers
*/
uint32_t countConnectedPeers(SceNetAdhocMatchingContext * context, const bool excludeTimedout = false);

/**
* Spawn Local Event for Event Thread
* @param context Matching Context Pointer
* @param event Event ID
* @param mac Event Source MAC
* @param optlen Optional Data Length
* @param opt Optional Data
*/
void spawnLocalEvent(SceNetAdhocMatchingContext * context, int event, SceNetEtherAddr * mac, int optlen, void * opt);

/*
 * Matching Event Thread (Send Ping and Hello Data) Part of AdhocMatching
 */
//int matchingEvent(int matchingId);
//int matchingEventThread(int matchingId); //(uint32_t args, void * argp)

/*
* Matching Input Thread (process Members) Part of AdhocMatching
*/
//int matchingInputThread(int matchingId); //(uint32_t args, void * argp)

/**
 * Return Number of active Peers in the same Network as the Local Player
 * @return Number of active Peers
 */
int getActivePeerCount(const bool excludeTimedout = true);

/**
 * Returns the locall Ip of this machine
 * @param SocketAddres OUT: local ip
 */
int getLocalIp(sockaddr_in * SocketAddress);
uint32_t getLocalIp(int sock);

/*
 * Check if an IP (big-endian/network order) is Private or Public IP
 */
bool isPrivateIP(uint32_t ip);

/*
 * Check if an IP (big-endian/network order) is Loopback IP
 */
bool isLoopbackIP(uint32_t ip);

/*
 * Get Number of bytes available in buffer to be Received
 * @param sock fd
 * @param udpBufferSize (UDP only)
 */
u_long getAvailToRecv(int sock, int udpBufferSize = 0);

/*
 * Get UDP Socket Max Message Size
 */
int getSockMaxSize(int udpsock);

/*
 * Get Socket Buffer Size (opt = SO_RCVBUF/SO_SNDBUF)
 */
int getSockBufferSize(int sock, int opt);

/*
* Set Socket Buffer Size (opt = SO_RCVBUF/SO_SNDBUF)
*/
int setSockBufferSize(int sock, int opt, int size);

/*
* Set TCP Socket Maximum Segment Size (default is 1460 on 1500 MTU)
*/
int setSockMSS(int sock, int size);

/*
* Set Socket TimeOut (opt = SO_SNDTIMEO/SO_RCVTIMEO)
*/
int setSockTimeout(int sock, int opt, unsigned long timeout_usec);

/*
 * Get Socket SO_ERROR (Requests and clears pending error information on the socket)
 */
int getSockError(int sock);

/*
 * Get TCP Socket TCP_NODELAY (Nagle Algo)
 */
int getSockNoDelay(int tcpsock);

/*
* Set TCP Socket TCP_NODELAY (Nagle Algo)
*/
int setSockNoDelay(int tcpsock, int flag);

/*
* Set Socket SO_NOSIGPIPE when supported
*/
int setSockNoSIGPIPE(int sock, int flag);

/*
* Set Socket SO_REUSEADDR and SO_REUSEPORT when supported
*/
int setSockReuseAddrPort(int sock);

/*
* Set Socket Connection Reset on UDP (which could cause a strange behavior)
*/
int setUDPConnReset(int udpsock, bool enabled);

/*
* Set Socket KeepAlive (opt = SO_KEEPALIVE)
*/
int setSockKeepAlive(int sock, bool keepalive, const int keepinvl = 60, const int keepcnt = 20, const int keepidle = 180);

/**
* Return the Number of Players with the chosen Nickname in the Local Users current Network
* @param nickname To-be-searched Nickname
* @return Number of matching Players
*/
int getNicknameCount(const char * nickname);


/**
 * Joins two 32 bits number into a 64 bit one
 * @param num1: first number
 * @param num2: second number
 * @return Single 64 bit number
 */
#define firstMask 0x00000000FFFFFFFF
#define secondMask 0xFFFFFFFF00000000
u64 join32(u32 num1, u32 num2);

/**
 * Splits a 64 bit number into two 32 bit ones
 * @param num: The number to be split
 * @param buf OUT: Array containing the split numbers
 */
void split64(u64 num, int buff[]);

/**
 * Returns the local mac
 * @param addr OUT: 6-bytes of Local Mac
 */
void getLocalMac(SceNetEtherAddr * addr);

/*
 * Returns the local port used by the socket
 */
uint16_t getLocalPort(int sock);

/**
* PDP Socket Counter
* @return Number of internal PDP Sockets
*/
int getPDPSocketCount();

/**
 * PTP Socket Counter
 * @return Number of internal PTP Sockets
 */
int getPTPSocketCount();

/**
 * Initialize Networking Components for Adhocctl Emulator
 * @param adhoc_id Game Product Code
 * @param server_ip Server IP
 * @return 0 on success or... -1
 */
int initNetwork(SceNetAdhocctlAdhocId *adhocid);

/**
 * Zero MAC Check
 * @param addr To-be-checked MAC Address
 * @return true if MAC is all zeroes
 */
bool isZeroMAC(const SceNetEtherAddr* addr);

/**
 * Broadcast MAC Check
 * @param addr To-be-checked MAC Address
 * @return true if Broadcast MAC or... 0
 */
bool isBroadcastMAC(const SceNetEtherAddr * addr);

/**
 * Resolve IP to MAC
 * @param ip Peer IP Address
 * @param mac OUT: Peer MAC
 * @return true on success
 */
bool resolveIP(uint32_t ip, SceNetEtherAddr * mac);

/**
 * Resolve MAC to IP
 * @param mac Peer MAC Address
 * @param ip OUT: Peer IP
 * @param port_offset OUT: Peer IP-specific Port Offset
 * @return true on success
 */
bool resolveMAC(SceNetEtherAddr* mac, uint32_t* ip, u16* port_offset = nullptr);

/**
 * Check whether Network Name contains only valid symbols
 * @param group_name To-be-checked Network Name
 * @return 1 if valid or... 0
 */
bool validNetworkName(const SceNetAdhocctlGroupName * groupname);

// Convert Matching Event Code to String
const char* getMatchingEventStr(int code);

// Convert Matching Opcode ID to String
const char* getMatchingOpcodeStr(int code);
