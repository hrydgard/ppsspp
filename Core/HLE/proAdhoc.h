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

#include "base/timeutil.h"
#include "base/mutex.h"
#include "thread/thread.h"
#include "net/resolve.h"
#include "Common/ChunkFile.h"

#include "Core/Config.h"
#include "Core/CoreTiming.h"
#include "Core/MemMap.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/sceNetAdhoc.h"
#include "Core/HLE/sceKernel.h"
#include "Core/HLE/sceKernelMutex.h"
#include "Core/HLE/sceUtility.h"

class PointerWrap;

// Net stuff
#ifdef _XBOX
#include <winsockx.h>
typedef int socklen_t;
#elif defined(_MSC_VER)
#include <WS2tcpip.h>
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#endif
#ifdef _MSC_VER
#define PACK
#undef errno
#undef EAGAIN
#undef EINPROGRESS
#undef EISCONN
#undef EALREADY
#define errno WSAGetLastError()
#define EAGAIN WSAEWOULDBLOCK
#define EINPROGRESS WSAEWOULDBLOCK
#define EISCONN WSAEISCONN
#define EALREADY WSAEALREADY
inline bool connectInProgress(int errcode){ return (errcode == WSAEWOULDBLOCK || errcode == WSAEINVAL || errcode == WSAEALREADY); }
#else
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#define closesocket close
#define PACK __attribute__((packed))
inline bool connectInProgress(int errcode){ return (errcode == EINPROGRESS); }
#endif

#define IsMatch(buf1, buf2)	(memcmp(&buf1, &buf2, sizeof(buf1)) == 0)

// psp strutcs and definitions
#define ADHOCCTL_MODE_ADHOC 0
#define ADHOCCTL_MODE_GAMEMODE 1

// Event Types for Event Handler
#define ADHOCCTL_EVENT_CONNECT 1
#define ADHOCCTL_EVENT_DISCONNECT 2
#define ADHOCCTL_EVENT_SCAN 3

// Internal Thread States
#define ADHOCCTL_STATE_DISCONNECTED 0
#define ADHOCCTL_STATE_CONNECTED 1
#define ADHOCCTL_STATE_SCANNING 2
#define ADHOCCTL_STATE_GAMEMODE 3

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
#define INET_POLLRDNORM 0x0040
#define INET_POLLWRNORM 0x0004

// Event Flags
#define ADHOC_EV_SEND 1
#define ADHOC_EV_RECV 2
#define ADHOC_EV_ALERT 0x400

// PTP Connection States
#define PTP_STATE_CLOSED 0
#define PTP_STATE_LISTEN 1
#define PTP_STATE_ESTABLISHED 4

// Alert Flags
#define ADHOC_F_ALERTSEND 0x0010
#define ADHOC_F_ALERTRECV 0x0020
#define ADHOC_F_ALERTPOLL 0x0040
#define ADHOC_F_ALERTCONNECT 0x0080
#define ADHOC_F_ALERTACCEPT 0x0100
#define ADHOC_F_ALERTFLUSH 0x0200
#define ADHOC_F_ALERTALL (ADHOC_F_ALERTSEND | ADHOC_F_ALERTRECV | ADHOC_F_ALERTPOLL | ADHOC_F_ALERTCONNECT | ADHOC_F_ALERTACCEPT | ADHOC_F_ALERTFLUSH)

// Timeouts
#define PSP_ADHOCCTL_PING_TIMEOUT 2000000

#ifdef _MSC_VER 
#pragma pack(push, 1)
#endif
// Ethernet Address
#define ETHER_ADDR_LEN 6
typedef struct SceNetEtherAddr {
  uint8_t data[ETHER_ADDR_LEN];
} PACK SceNetEtherAddr;

// Broadcast MAC
extern uint8_t broadcastMAC[ETHER_ADDR_LEN];

// Malloc Pool Information
typedef struct SceNetMallocStat {
	s32_le poolsize;
	s32_le maxsize;
	s32_le freesize;
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

// Active Virtual Network Information
typedef struct SceNetAdhocctlParameter {
  s32_le channel;
  SceNetAdhocctlGroupName group_name;
  SceNetAdhocctlBSSId bssid;
  SceNetAdhocctlNickname nickname;
} PACK SceNetAdhocctlParameter;

// Peer Information
typedef struct SceNetAdhocctlPeerInfo {
  SceNetAdhocctlPeerInfo * next;
  SceNetAdhocctlNickname nickname;
  SceNetEtherAddr mac_addr;
  u32_le ip_addr;
  uint8_t padding[2];
  u64_le last_recv; // Need to use the same method with sceKernelGetSystemTimeWide (ie. CoreTiming::GetGlobalTimeUsScaled) to prevent timing issue (ie. in game timeout)
} PACK SceNetAdhocctlPeerInfo;

// Peer Information with u32 pointers
typedef struct SceNetAdhocctlPeerInfoEmu {
  u32_le next; // Changed the pointer to u32
  SceNetAdhocctlNickname nickname;
  SceNetEtherAddr mac_addr;
  u32_le ip_addr; //jpcsp wrote 6bytes of 0x11 for this & padding
  u16 padding; // Changed the padding to u16
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
  SceNetEtherAddr member[ADHOCCTL_GAMEMODE_MAX_MEMBERS];
} PACK SceNetAdhocctlGameModeInfo;

// Socket Polling Event Listener
typedef struct SceNetAdhocPollSd{
  s32_le id;
  s32_le events;
  s32_le revents;
} PACK SceNetAdhocPollSd;

// PDP Socket Status
typedef struct SceNetAdhocPdpStat{
  u32_le next; // struct SceNetAdhocPdpStat * next;
  s32_le id;
  SceNetEtherAddr laddr;
  u16_le lport;
  u32_le rcv_sb_cc;
} PACK SceNetAdhocPdpStat;

// PTP Socket Status
typedef struct SceNetAdhocPtpStat {
  u32_le next; // Changed the pointer to u32
  s32_le id;
  SceNetEtherAddr laddr;
  SceNetEtherAddr paddr;
  u16_le lport;
  u16_le pport;
  s32_le snd_sb_cc;
  s32_le rcv_sb_cc;
  s32_le state;
} PACK SceNetAdhocPtpStat;

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
#ifdef _MSC_VER 
#pragma pack(pop)
#endif

// Adhoc ID (Game Product Key)
#define ADHOCCTL_ADHOCID_LEN 9
typedef struct SceNetAdhocctlAdhocId {
	s32_le type;
	uint8_t data[ADHOCCTL_ADHOCID_LEN];
	uint8_t padding[3];
} SceNetAdhocctlAdhocId; // should this be packed?

// Polling Event Field
typedef struct SceNetInetPollfd {
	s32_le fd;
	s16_le events;
	s16_le revents;
} SceNetInetPollfd; // should this be packed?

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
  struct SceNetAdhocMatchingContext * next;

  // Externally Visible ID
  s32_le id;

  // Matching Mode (HOST, CLIENT, P2P)
  s32_le mode;

  // Running Flag (1 = running, 0 = created)
  s32_le running;

  // Maximum Number of Peers (for HOST, P2P)
  s32_le maxpeers;

  // Local MAC Address
  SceNetEtherAddr mac;

  // Peer List for Connectees
  SceNetAdhocMatchingMemberInternal * peerlist; // SceNetAdhocMatchingMemberInfo[Emu]

  // Local PDP Port
  u16_le port;

  // Local PDP Socket
  s32_le socket;

  // Receive Buffer Length
  s32_le rxbuflen;

  // Receive Buffer
  uint8_t * rxbuf;

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
  //SceNetAdhocMatchingHandlerArgs handlerArgs;

  // Hello Data Length
  s32_le hellolen;

  // Hello Data Address
  u32_le helloAddr;

  // Hello Data
  uint8_t * hello;

  // Event Caller Thread
  std::thread eventThread; // s32_le event_thid;
  bool eventRunning = false;
  bool IsMatchingInCB = false;

  // IO Handler Thread
  std::thread inputThread; // s32_le input_thid;
  bool inputRunning = false;

  // Event Caller Thread Message Stack
  recursive_mutex * eventlock; // s32_le event_stack_lock;
  ThreadMessage * event_stack;

  // IO Handler Thread Message Stack
  recursive_mutex * inputlock; // s32_le input_stack_lock;
  ThreadMessage * input_stack;

  // Socket Connectivity
  //bool connected = false;
  //bool InConnection = false;
  //u32_le handlerid = -1;
  //int eventMatchingHandlerUpdate = -1;
} SceNetAdhocMatchingContext;

// End of psp definitions

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

// Matching modes
#define PSP_ADHOC_MATCHING_MODE_PARENT			1
#define PSP_ADHOC_MATCHING_MODE_CHILD			2
#define PSP_ADHOC_MATCHING_MODE_P2P				3

// Matching Events
#define PSP_ADHOC_MATCHING_EVENT_HELLO			1
#define PSP_ADHOC_MATCHING_EVENT_REQUEST		2
#define PSP_ADHOC_MATCHING_EVENT_LEAVE			3
#define PSP_ADHOC_MATCHING_EVENT_DENY			4
#define PSP_ADHOC_MATCHING_EVENT_CANCEL			5
#define PSP_ADHOC_MATCHING_EVENT_ACCEPT			6
#define PSP_ADHOC_MATCHING_EVENT_ESTABLISHED	7
#define PSP_ADHOC_MATCHING_EVENT_TIMEOUT		8
#define PSP_ADHOC_MATCHING_EVENT_ERROR			9
#define PSP_ADHOC_MATCHING_EVENT_BYE			10
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

// Nonblocking Flag for Adhoc API
#define PSP_ADHOC_F_NONBLOCK 1

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

// C2S Chat Packet
typedef struct {
  SceNetAdhocctlPacketBase base;
  char message[64];
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

class AfterMatchingMipsCall : public Action {
public:
	AfterMatchingMipsCall() {}
	static Action *Create() { return new AfterMatchingMipsCall(); }
	void DoState(PointerWrap &p) {
		auto s = p.Section("AfterMatchingMipsCall", 1, 2);
		if (!s)
			return;

		p.Do(ID);
	}
	void run(MipsCall &call);
	void SetID(u32 Id) { ID = Id; }

private:
	u32 ID;
};

extern int actionAfterMatchingMipsCall;
extern bool IsAdhocctlInCB;

// Aux vars
extern int metasocket;
extern SceNetAdhocctlParameter parameter;
extern SceNetAdhocctlAdhocId product_code;
extern std::thread friendFinderThread;
extern recursive_mutex peerlock;
extern SceNetAdhocPdpStat * pdp[255];
extern SceNetAdhocPtpStat * ptp[255];
extern std::map<int, AdhocctlHandler> adhocctlHandlers;

extern uint32_t fakePoolSize;
extern SceNetAdhocMatchingContext * contexts;
extern int one;                 
extern bool friendFinderRunning;
extern SceNetAdhocctlPeerInfo * friends;
extern SceNetAdhocctlScanInfo * networks; 
extern int eventAdhocctlHandlerUpdate;
extern int eventMatchingHandlerUpdate;
extern int threadStatus;
// End of Aux vars

// Check if Matching callback is running
bool IsMatchingInCallback(SceNetAdhocMatchingContext * context);

/**
 * Local MAC Check
 * @param saddr To-be-checked MAC Address
 * @return 1 if valid or... 0
 */
int isLocalMAC(const SceNetEtherAddr * addr);

/**
 * PDP Port Check
 * @param port To-be-checked Port
 * @return 1 if in use or... 0
 */
int isPDPPortInUse(uint16_t port);

/**
 * Check whether PTP Port is in use or not
 * @param port To-be-checked Port Number
 * @return 1 if in use or... 0
 */
int isPTPPortInUse(uint16_t port);

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

/*
 * Find a Peer/Friend by MAC address
 */
SceNetAdhocctlPeerInfo * findFriend(SceNetEtherAddr * MAC);

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
int countAvailableNetworks();

/*
 * Find an existing group in networks
 */
SceNetAdhocctlScanInfo * findGroup(SceNetEtherAddr * MAC);

/*
* Deletes all groups in networks
*/
void freeGroupsRecursive(SceNetAdhocctlScanInfo * node);

/**
 * Closes & Deletes all PDP Sockets
 */
void deleteAllPDP(void);

/**
 * Closes & Deletes all PTP sockets
 */
void deleteAllPTP(void);

/**
 * Delete Friend from Local List
 * @param ip Friend IP
 */
void deleteFriendByIP(uint32_t ip);

/**
 * Recursive Memory Freeing-Helper for Friend-Structures
 * @param node Current Node in List
 */
void freeFriendsRecursive(SceNetAdhocctlPeerInfo * node);

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
int findFreeMatchingID(void);

/**
* Find Internal Matching Context for Matching ID
* @param id Matching ID
* @return Matching Context Pointer or... NULL
*/
SceNetAdhocMatchingContext * findMatchingContext(int id);

/*
* Notify Matching Event Handler
*/
void notifyMatchingHandler(SceNetAdhocMatchingContext * context, ThreadMessage * msg, void * opt, u32 &bufAddr, u32 &bufLen, u32_le * args);
// Notifiy Adhocctl Handlers
void notifyAdhocctlHandlers(int flag, int error);

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
s32_le countChildren(SceNetAdhocMatchingContext * context);

/**
* Delete Peer from List
* @param context Matching Context Pointer
* @param peer Internal Peer Reference
*/
void deletePeer(SceNetAdhocMatchingContext * context, SceNetAdhocMatchingMemberInternal * peer);

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
SceNetAdhocMatchingMemberInternal * findP2P(SceNetAdhocMatchingContext * context);

/**
* Return Number of Connected Peers
* @param context Matching Context Pointer
* @return Number of Connected Peers
*/
uint32_t countConnectedPeers(SceNetAdhocMatchingContext * context);

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
int getActivePeerCount(void);

/**
 * Returns the locall Ip of this machine, TODO: Implement the linux version
 * @param SocketAddres OUT: local ip
 */
int getLocalIp(sockaddr_in * SocketAddress);
uint32_t getLocalIp(int sock);

/*
 * Get Socket Buffer Size (opt = SO_RCVBUF/SO_SNDBUF)
 */
int getSockBufferSize(int sock, int opt);

/*
* Set Socket Buffer Size (opt = SO_RCVBUF/SO_SNDBUF)
*/
int setSockBufferSize(int sock, int opt, int size);

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
 * Returns the local mac, TODO: Read from Config file
 * @param addr OUT: Local Mac
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
int getPDPSocketCount(void);

/**
 * PTP Socket Counter
 * @return Number of internal PTP Sockets
 */
int getPTPSocketCount(void);

/**
 * Initialize Networking Components for Adhocctl Emulator
 * @param adhoc_id Game Product Code
 * @param server_ip Server IP
 * @return 0 on success or... -1
 */
int initNetwork(SceNetAdhocctlAdhocId *adhocid);

/**
 * Broadcast MAC Check
 * @param addr To-be-checked MAC Address
 * @return 1 if Broadcast MAC or... 0
 */
int isBroadcastMAC(const SceNetEtherAddr * addr);

/**
 * Resolve IP to MAC
 * @param ip Peer IP Address
 * @param mac OUT: Peer MAC
 * @return 0 on success or... ADHOC_NO_ENTRY
 */
int resolveIP(uint32_t ip, SceNetEtherAddr * mac);

/**
 * Resolve MAC to IP
 * @param mac Peer MAC Address
 * @param ip OUT: Peer IP
 * @return 0 on success or... ADHOC_NO_ENTRY
 */
int resolveMAC(SceNetEtherAddr * mac, uint32_t * ip);

/**
 * Check whether Network Name contains only valid symbols
 * @param group_name To-be-checked Network Name
 * @return 1 if valid or... 0
 */
 int validNetworkName(const SceNetAdhocctlGroupName * groupname);

 // Convert Matching Event Code to String
 char* getMatchingEventStr(int code, char* buf);

 // Convert Matching Opcode ID to String
 char* getMatchingOpcodeStr(int code, char* buf);

