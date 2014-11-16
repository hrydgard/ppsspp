#pragma once

#include "base/timeutil.h"
#include "base/mutex.h"
#include "thread/thread.h"
#include "net/resolve.h"

#include "Core/Config.h"
#include "Core/CoreTiming.h"
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
#define errno WSAGetLastError()
#define EAGAIN WSAEWOULDBLOCK
#define EINPROGRESS WSAEWOULDBLOCK
#define EISCONN WSAEISCONN
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

// PTP Connection States
#define PTP_STATE_CLOSED 0
#define PTP_STATE_LISTEN 1
#define PTP_STATE_ESTABLISHED 4

#ifdef _MSC_VER 
#pragma pack(push, 1)
#endif
// Ethernet Address
#define ETHER_ADDR_LEN 6
typedef struct SceNetEtherAddr {
  uint8_t data[ETHER_ADDR_LEN];
} PACK SceNetEtherAddr;


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
  u64_le last_recv;
} PACK SceNetAdhocctlPeerInfo;

// Peer Information with u32 pointers
typedef struct SceNetAdhocctlPeerInfoEmu {
  u32_le next; // Changed the pointer to u32
  SceNetAdhocctlNickname nickname;
  SceNetEtherAddr mac_addr;
  u32_le ip_addr;
  u32 padding; // Changed the pointer to u32
  u64_le last_recv;
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
#ifdef _MSC_VER 
#pragma pack(pop)
#endif

// Adhoc ID (Game Product Key)
#define ADHOCCTL_ADHOCID_LEN 9
typedef struct SceNetAdhocctlAdhocId {
  s32_le type;
  uint8_t data[ADHOCCTL_ADHOCID_LEN];
  uint8_t padding[3];
} SceNetAdhocctlAdhocId;

// Socket Polling Event Listener
struct SceNetAdhocPollSd {
  s32_le id;
  s32_le events;
  s32_le revents;
};

// PDP Socket Status
struct SceNetAdhocPdpStat {
  struct SceNetAdhocPdpStat * next;
  s32_le id;
  SceNetEtherAddr laddr;
  u16_le lport;
  u32_le rcv_sb_cc;
};

// PTP Socket Status
struct SceNetAdhocPtpStat {
  u32_le next; // Changed the pointer to u32
  s32_le id;
  SceNetEtherAddr laddr;
  SceNetEtherAddr paddr;
  u16_le lport;
  u16_le pport;
  u32_le snd_sb_cc;
  u32_le rcv_sb_cc;
  s32_le state;
};

// Gamemode Optional Peer Buffer Data
struct SceNetAdhocGameModeOptData {
  u32_le size;
  u32_le flag;
  u64_le last_recv;
};

// Gamemode Buffer Status
struct SceNetAdhocGameModeBufferStat {
  struct SceNetAdhocGameModeBufferStat * next;
  s32_le id;
  void * ptr;
  u32_le size;
  u32_le master;
  SceNetAdhocGameModeOptData opt;
};


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
  u64_le lastping;
} SceNetAdhocMatchingMemberInternal;


// Matching handler
struct SceNetAdhocMatchingHandlerArgs {
  s32_le id;
  s32_le event;
  SceNetEtherAddr * peer;
  s32_le optlen;
  void * opt;
};

struct SceNetAdhocMatchingHandler {
  u32_le entryPoint;
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

  // Socket Connectivity
  //bool connected;

  // Hello Data Length
  u32_le hellolen;

  // Hello Data Address
  u32_le helloAddr;

  // Hello Data
  void * hello;

  // Event Caller Thread
  s32_le event_thid;

  // IO Handler Thread
  s32_le input_thid;

  // Event Caller Thread Message Stack
  s32_le event_stack_lock;
  ThreadMessage * event_stack;

  // IO Handler Thread Message Stack
  s32_le input_stack_lock;
  ThreadMessage * input_stack;
} SceNetAdhocMatchingContext;

// End of psp definitions

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
#ifdef _MSC_VER 
#pragma pack(pop)
#endif

// Aux vars
extern int metasocket;
extern SceNetAdhocctlParameter parameter;
extern std::thread friendFinderThread;
extern recursive_mutex peerlock;
extern SceNetAdhocPdpStat * pdp[255];
extern SceNetAdhocPtpStat * ptp[255];

extern uint32_t fakePoolSize;
extern SceNetAdhocMatchingContext * contexts;
extern int one;                 
extern bool friendFinderRunning;
extern SceNetAdhocctlPeerInfo * friends;
extern SceNetAdhocctlScanInfo * networks; 
extern int eventHandlerUpdate;
extern int threadStatus;
// End of Aux vars

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
SceNetAdhocMatchingMemberInternal* findMember(SceNetAdhocMatchingContext * context, SceNetEtherAddr * mac);
void addMember(SceNetAdhocMatchingContext * context, SceNetEtherAddr * mac);
void deleteMember(SceNetAdhocMatchingContext * context, SceNetEtherAddr * mac);
void deleteAllMembers(SceNetAdhocMatchingContext * context);


/**
 * Add Friend to Local List
 * @param packet Friend Information
 */
void addFriend(SceNetAdhocctlConnectPacketS2C * packet);

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
