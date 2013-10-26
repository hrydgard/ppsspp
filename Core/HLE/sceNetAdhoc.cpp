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


// sceNetAdhoc

// These acronyms are seen in function names:
// * PDP: a proprietary Sony protocol similar to UDP.
// * PTP: a proprietary Sony protocol similar to TCP.

// We will need to wrap them into the similar UDP and TCP messages. If we want to
// play adhoc remotely online, I guess we'll need to wrap both into TCP/IP.

// We will need some server infrastructure to provide match making. We'll
// group players per game. Maybe allow players to join rooms and then start the game,
// instead of the other way around?

#include "../Config.h"
#include "Core/HLE/HLE.h"
#include "../CoreTiming.h"
#include "Core/HLE/sceNetAdhoc.h"

#include "sceKernel.h"
#include "sceKernelThread.h"
#include "sceKernelMutex.h"
#include "sceUtility.h"
#include "net/resolve.h"

// Temporary stuff
#include <thread>
#ifdef _WIN32
#include <WS2tcpip.h>
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#define Sleep sleep
#define closesocket close
#endif
#include <mutex>
// End of extra includes

enum {
  ERROR_NET_ADHOC_INVALID_SOCKET_ID            = 0x80410701,
  ERROR_NET_ADHOC_INVALID_ADDR                 = 0x80410702,
  ERROR_NET_ADHOC_INVALID_ARG                  = 0x80410B04,
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
  ERROR_NET_ADHOC_WOULD_BLOCK                  = 0x80410709,
  ERROR_NET_ADHOC_INVALID_DATALEN              = 0x80410705,
  ERROR_NET_ADHOC_INVALID_PORT                 = 0x80410703,

  ERROR_NET_ADHOCCTL_WLAN_SWITCH_OFF           = 0x80410b03,
  ERROR_NET_ADHOCCTL_ALREADY_INITIALIZED       = 0x80410b07,
  ERROR_NET_ADHOCCTL_NOT_INITIALIZED           = 0x80410b08,
  ERROR_NET_ADHOCCTL_DISCONNECTED              = 0x80410b09,
  ERROR_NET_ADHOCCTL_BUSY                      = 0x80410b10,
  ERROR_NET_ADHOCCTL_TOO_MANY_HANDLERS         = 0x80410b12,

  ERROR_NET_NO_SPACE                           = 0x80410001, 
};

enum {
  PSP_ADHOC_POLL_READY_TO_SEND = 1,
  PSP_ADHOC_POLL_DATA_AVAILABLE = 2,
  PSP_ADHOC_POLL_CAN_CONNECT = 4,
  PSP_ADHOC_POLL_CAN_ACCEPT = 8,
};

const size_t MAX_ADHOCCTL_HANDLERS = 32;

static bool netAdhocInited;
static bool netAdhocctlInited;
static bool netAdhocMatchingInited;

// These might come in handy in the future, if PPSSPP ever supports wifi/ad-hoc..
struct SceNetAdhocctlParams {
  s32_le channel; //which ad-hoc channel to connect to
  char name[8]; //connection name
  u8 bssid[6];  //BSSID of the connection?
  char nickname[128]; //PSP's nickname?
};

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

// Ethernet Address (MAC)
#define ETHER_ADDR_LEN 6
typedef struct SceNetEtherAddr {
  uint8_t data[ETHER_ADDR_LEN];
} SceNetEtherAddr;

// Adhoc Virtual Network Name (1234ABCD)
#define ADHOCCTL_GROUPNAME_LEN 8
typedef struct SceNetAdhocctlGroupName {
  uint8_t data[ADHOCCTL_GROUPNAME_LEN];
} SceNetAdhocctlGroupName;

// Player Nickname
#define ADHOCCTL_NICKNAME_LEN 128
typedef struct SceNetAdhocctlNickname {
  uint8_t data[ADHOCCTL_NICKNAME_LEN];
} SceNetAdhocctlNickname;

// Adhoc ID (Game Product Key)
#define ADHOCCTL_ADHOCID_LEN 9
typedef struct SceNetAdhocctlAdhocId {
  int type;
  uint8_t data[ADHOCCTL_ADHOCID_LEN];
  uint8_t padding[3];
} SceNetAdhocctlAdhocId;

// Virtual Network Host Information
typedef struct SceNetAdhocctlBSSId {
  SceNetEtherAddr mac_addr;
  uint8_t padding[2];
};

// Virtual Network Information
typedef struct SceNetAdhocctlScanInfo {
  struct SceNetAdhocctlScanInfo * next;
  int channel;
  SceNetAdhocctlGroupName group_name;
  SceNetAdhocctlBSSId bssid;
  int mode;
};
// Active Virtual Network Information
typedef struct SceNetAdhocctlParameter {
  int channel;
  SceNetAdhocctlGroupName group_name;
  SceNetAdhocctlBSSId bssid;
  SceNetAdhocctlNickname nickname;
};

// Peer Information
typedef struct SceNetAdhocctlPeerInfo {
  struct SceNetAdhocctlPeerInfo * next;
  SceNetAdhocctlNickname nickname;
  SceNetEtherAddr mac_addr;
  uint32_t ip_addr;
  uint8_t padding[2];
  uint64_t last_recv;
};

// Game Mode Peer List
#define ADHOCCTL_GAMEMODE_MAX_MEMBERS 16
typedef struct SceNetAdhocctlGameModeInfo {
  int num;
  SceNetEtherAddr member[ADHOCCTL_GAMEMODE_MAX_MEMBERS];
};

// Socket Polling Event Listener
typedef struct SceNetAdhocPollSd {
  int id;
  int events;
  int revents;
};

// PDP Socket Status
typedef struct SceNetAdhocPdpStat {
  struct SceNetAdhocPdpStat * next;
  int id;
  SceNetEtherAddr laddr;
  uint16_t lport;
  uint32_t rcv_sb_cc;
};

// PTP Socket Status
typedef struct SceNetAdhocPtpStat {
  struct SceNetAdhocPtpStat * next;
  int id;
  SceNetEtherAddr laddr;
  SceNetEtherAddr paddr;
  uint16_t lport;
  uint16_t pport;
  uint32_t snd_sb_cc;
  uint32_t rcv_sb_cc;
  int state;
};

// Gamemode Optional Peer Buffer Data
typedef struct SceNetAdhocGameModeOptData {
  uint32_t size;
  uint32_t flag;
  uint64_t last_recv;
};

// Gamemode Buffer Status
typedef struct SceNetAdhocGameModeBufferStat {
  struct SceNetAdhocGameModeBufferStat * next;
  int id;
  void * ptr;
  uint32_t size;
  uint32_t master;
  SceNetAdhocGameModeOptData opt;
};
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
typedef struct
{
  // Game Product Code (ex. ULUS12345)
  char data[PRODUCT_CODE_LENGTH];
} __attribute__((packed)) SceNetAdhocctlProductCode;

// Basic Packet
typedef struct
{
  uint8_t opcode;
} __attribute__((packed)) SceNetAdhocctlPacketBase;

// C2S Login Packet
typedef struct
{
  SceNetAdhocctlPacketBase base;
  SceNetEtherAddr mac;
  SceNetAdhocctlNickname name;
  SceNetAdhocctlProductCode game;
} __attribute__((packed)) SceNetAdhocctlLoginPacketC2S;

// C2S Connect Packet
typedef struct
{
  SceNetAdhocctlPacketBase base;
  SceNetAdhocctlGroupName group;
} __attribute__((packed)) SceNetAdhocctlConnectPacketC2S;

// C2S Chat Packet
typedef struct
{
  SceNetAdhocctlPacketBase base;
  char message[64];
} __attribute__((packed)) SceNetAdhocctlChatPacketC2S;

// S2C Connect Packet
typedef struct
{
  SceNetAdhocctlPacketBase base;
  SceNetAdhocctlNickname name;
  SceNetEtherAddr mac;
  uint32_t ip;
} __attribute__((packed)) SceNetAdhocctlConnectPacketS2C;

// S2C Disconnect Packet
typedef struct
{
  SceNetAdhocctlPacketBase base;
  uint32_t ip;
} __attribute__((packed)) SceNetAdhocctlDisconnectPacketS2C;

// S2C Scan Packet
typedef struct
{
  SceNetAdhocctlPacketBase base;
  SceNetAdhocctlGroupName group;
  SceNetEtherAddr mac;
} __attribute__((packed)) SceNetAdhocctlScanPacketS2C;

// S2C Connect BSSID Packet
typedef struct
{
  SceNetAdhocctlPacketBase base;
  SceNetEtherAddr mac;
} __attribute__((packed)) SceNetAdhocctlConnectBSSIDPacketS2C;

// S2C Chat Packet
typedef struct
{
  SceNetAdhocctlChatPacketC2S base;
  SceNetAdhocctlNickname name;
} __attribute__((packed)) SceNetAdhocctlChatPacketS2C;

// Aux vars
static int metasocket;
static int eventHandlerUpdate = -1;
static int threadStatus = ADHOCCTL_STATE_DISCONNECTED;
static SceNetAdhocctlParameter parameter;
static std::thread friendFinderThread;
static SceNetAdhocctlPeerInfo * friends = NULL;
static SceNetAdhocctlScanInfo * networks = NULL;
static std::mutex peerlock;
static SceNetAdhocPdpStat * pdp[255];
static SceNetAdhocPtpStat * ptp[255];
// End of Aux vars

struct AdhocctlHandler {
  u32 entryPoint;
  u32 argument;
};

static std::map<int, AdhocctlHandler> adhocctlHandlers;

void __NetAdhocInit() {
  netAdhocInited = false;
  netAdhocctlInited = false;
  netAdhocMatchingInited = false;
  adhocctlHandlers.clear();
}

void __NetAdhocShutdown() {

}

void __NetAdhocDoState(PointerWrap &p) {
  auto s = p.Section("sceNetAdhoc", 1);
  if (!s)
    return;

  p.Do(netAdhocInited);
  p.Do(netAdhocctlInited);
  p.Do(netAdhocMatchingInited);
  p.Do(adhocctlHandlers);
}

void __UpdateAdhocctlHandlers(int flag, int error) {
  u32 args[3] = { 0, 0, 0 };
  args[0] = flag;
  args[1] = error;

  for (std::map<int, AdhocctlHandler>::iterator it = adhocctlHandlers.begin(); it != adhocctlHandlers.end(); ++it) {
    args[2] = it->second.argument;

    __KernelDirectMipsCall(it->second.entryPoint, NULL, args, 3, true);
  }
}

#define firstMask 0x00000000FFFFFFFF
#define secondMask 0xFFFFFFFF00000000
void split64(u64 num, int buff[]){
  int num1 = (int)(num&firstMask);
  int num2 = (int)((num&secondMask)>>32);
  buff[0] = num1;
  buff[1] = num2;
}

u64 join32(u32 num1, u32 num2){
  return (u64)num2 << 32 | num1;
}

// Gets the local mac TODO: Read from config
void __getLocalMac(SceNetEtherAddr * addr){
  uint8_t mac[] = {1, 2, 3, 4, 5, 6};
  memcpy(addr,mac,ETHER_ADDR_LEN);
}

// This only usefull if the server is running on the same machine as the client
int __getLocalIp(sockaddr_in * SocketAddress){
#ifdef _WIN32
  // Get local host name
  char szHostName[128] = "";

  if(::gethostname(szHostName, sizeof(szHostName))) {
    // Error handling 
  }
  // Get local IP addresses
  struct hostent     *pHost        = 0;
  pHost = ::gethostbyname(szHostName);
  if(pHost) {
    memcpy(&SocketAddress->sin_addr, pHost->h_addr_list[0], pHost->h_length);
    return 0;
  }
  return -1;
#else
  char ip[] = "192.168.12.1";
  SocketAddress->sin_addr.s_addr = inet_addr("192.168.12.1");
  return 0;
#endif
}

// Returns current system time in miliseconds, windows only!
long long __milliseconds_now() {
#ifdef _WIN32
  static LARGE_INTEGER s_frequency;
  static BOOL s_use_qpc = QueryPerformanceFrequency(&s_frequency);
  if (s_use_qpc) {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return (1000LL * now.QuadPart) / s_frequency.QuadPart;
  } else {
    return GetTickCount();
  }
#else
  timeval val;
  gettimeofday(&val,NULL);
  return val.tv_usec * 1000;
#endif
}

/* chages to the blocking mode of the socket
   1 to set noblocking
   0 to set blocking
*/
void __change_blocking_mode(int fd, int nonblocking) {
  unsigned long on = 1;
  unsigned long off = 0;
#ifdef _WIN32
  if(nonblocking){
    // Change to Non-Blocking Mode
    ioctlsocket(fd,FIONBIO,&on);
  }else { 
    // Change to Blocking Mode
    ioctlsocket(fd, FIONBIO, &off);
  }
#else
    if(nonblocking) fcntl(fd, F_SETFL, O_NONBLOCK);
    else {
      // Get Flags
      int flags = fcntl(fd, F_GETFL);
      // Remove Non-Blocking Flag
      fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
    }
#endif
}

// Inits all basic net functionality and logins the user to the main server
int __init_network(SceNetAdhocctlAdhocId *adhoc_id){
  int iResult = 0;
#ifdef _WIN32
  WSADATA data;
  iResult = WSAStartup(MAKEWORD(2,2),&data);
  if(iResult != NOERROR){
    printf("Wsa failed with error %d\n",iResult);
    return iResult;
  }
#endif
  metasocket = INVALID_SOCKET;
  metasocket = socket(AF_INET,SOCK_STREAM, IPPROTO_TCP);
  if(metasocket == INVALID_SOCKET){
    printf("invalid socket");
    return INVALID_SOCKET;
  }
  struct sockaddr_in server_addr;
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(27312);
  server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
  iResult = connect(metasocket,(sockaddr *)&server_addr,sizeof(server_addr));

  if(iResult == SOCKET_ERROR){
    printf("Socket error %d", iResult);
    return iResult;
  }

  SceNetAdhocctlLoginPacketC2S packet;
  packet.base.opcode = OPCODE_LOGIN;
  SceNetEtherAddr addres;
  __getLocalMac(&addres);
  packet.mac = addres;
  strcpy((char *)packet.name.data, g_Config.sNickName.c_str());
  memcpy(packet.game.data, adhoc_id->data, ADHOCCTL_ADHOCID_LEN);
  int sent = send(metasocket, (char*)&packet, sizeof(packet), 0);
  __change_blocking_mode(metasocket,1); // Change to non-blocking
  if(sent > 0){
    return 0;
  }else{
    return -1;
  }
}

/**
 * Add Friend to Local List
 * @param packet Friend Information
 */
void __addFriend(SceNetAdhocctlConnectPacketS2C * packet) {
  // Allocate Structure
  SceNetAdhocctlPeerInfo * peer = (SceNetAdhocctlPeerInfo *)malloc(sizeof(SceNetAdhocctlPeerInfo));
  // Allocated Structure
  if(peer != NULL)
  {
    // Clear Memory
    memset(peer, 0, sizeof(SceNetAdhocctlPeerInfo));

    // Link to existing Peers
    peer->next = friends;

    // Save Nickname
    peer->nickname = packet->name;

    // Save MAC Address
    peer->mac_addr = packet->mac;

    // Save IP Address
    peer->ip_addr = packet->ip;

    // Multithreading Lock
    peerlock.lock();

    // Link into Peerlist
    friends = peer;

    // Multithreading Unlock
    peerlock.unlock();
  }
}

/**
 * Delete Friend from Local List
 * @param ip Friend IP
 */
void __deleteFriendByIP(uint32_t ip) {
  // Previous Peer Reference
  SceNetAdhocctlPeerInfo * prev = NULL;

  // Peer Pointer
  SceNetAdhocctlPeerInfo * peer = friends;

  // Iterate Peers
  for(; peer != NULL; peer = peer->next)
  {
    // Found Peer
    if(peer->ip_addr == ip)
    {
      // Multithreading Lock
      peerlock.lock();

      // Unlink Left (Beginning)
      if(prev == NULL)friends = peer->next;

      // Unlink Left (Other)
      else prev->next = peer->next;

      // Multithreading Unlock
      peerlock.unlock();

      // Free Memory
      free(peer);

      // Stop Search
      break;
    }

    // Set Previous Reference
    prev = peer;
  }
}

/**
 * Recursive Memory Freeing-Helper for Friend-Structures
 * @param node Current Node in List
 */
void __freeFriendsRecursive(SceNetAdhocctlPeerInfo * node) {
  // End of List
  if(node == NULL) return;

  // Increase Recursion Depth
  __freeFriendsRecursive(node->next);

  // Free Memory
  free(node);
}

// Thread that listens to all relevant messages from the server
int __friendFinder(){
  // Receive Buffer
  int rxpos = 0;
  uint8_t rx[1024];

  // Chat Packet
  SceNetAdhocctlChatPacketC2S chat;
  chat.base.opcode = OPCODE_CHAT;

  // Last Ping Time
  long long lastping = 0;

  // Last Time Reception got updated
  uint64_t lastreceptionupdate = 0;

  // Finder Loop
  while(netAdhocctlInited) {
    // Acquire Network Lock
    //_acquireNetworkLock();

    // Ping Server

    if(__milliseconds_now() - lastping >= 100) {
      // Update Ping Time
      lastping = __milliseconds_now();

      // Prepare Packet
      uint8_t opcode = OPCODE_PING;

      // Send Ping to Server
      send(metasocket, (const char *)&opcode, 1,0);
    }

    // Send Chat Messages
    //while(popFromOutbox(chat.message))
    //{
    //  // Send Chat to Server
    //  sceNetInetSend(metasocket, (const char *)&chat, sizeof(chat), 0);
    //}

    // Wait for Incoming Data
    int received = recv(metasocket, (char *)(rx + rxpos), sizeof(rx) - rxpos,0);

    // Free Network Lock
    //_freeNetworkLock();

    // Received Data
    if(received > 0) {
      // Fix Position
      rxpos += received;

      // Log Incoming Traffic
      //printf("Received %d Bytes of Data from Server\n", received);
      INFO_LOG(SCENET, "Received %d Bytes of Data from Adhoc Server", received);
    }

    // Handle Packets
    if(rxpos > 0) {
      // BSSID Packet
      if(rx[0] == OPCODE_CONNECT_BSSID) {
        // Enough Data available
        if(rxpos >= sizeof(SceNetAdhocctlConnectBSSIDPacketS2C)) {
          // Cast Packet
          SceNetAdhocctlConnectBSSIDPacketS2C * packet = (SceNetAdhocctlConnectBSSIDPacketS2C *)rx;
          // Update BSSID
          parameter.bssid.mac_addr = packet->mac;
          // Change State
          threadStatus = ADHOCCTL_STATE_CONNECTED;
          // Notify Event Handlers
          CoreTiming::ScheduleEvent_Threadsafe_Immediate(eventHandlerUpdate, join32(ADHOCCTL_EVENT_CONNECT, 0));

          // Move RX Buffer
          memmove(rx, rx + sizeof(SceNetAdhocctlConnectBSSIDPacketS2C), sizeof(rx) - sizeof(SceNetAdhocctlConnectBSSIDPacketS2C));

          // Fix RX Buffer Length
          rxpos -= sizeof(SceNetAdhocctlConnectBSSIDPacketS2C);
        }
      }

      // Chat Packet
      else if(rx[0] == OPCODE_CHAT) {
        // Enough Data available
        if(rxpos >= sizeof(SceNetAdhocctlChatPacketS2C)) {
          // Cast Packet
          SceNetAdhocctlChatPacketS2C * packet = (SceNetAdhocctlChatPacketS2C *)rx;

          // Fix for Idiots that try to troll the "ME" Nametag
#ifdef _WIN32 
          if(stricmp((char *)packet->name.data, "ME") == 0) strcpy((char *)packet->name.data, "NOT ME");
#else
          if(strcasecmp((char *)packet->name.data, "ME") == 0) strcpy((char *)packet->name.data, "NOT ME");
#endif

          // Add Incoming Chat to HUD
          //printf("Receive chat message %s", packet->base.message);

          // Move RX Buffer
          memmove(rx, rx + sizeof(SceNetAdhocctlChatPacketS2C), sizeof(rx) - sizeof(SceNetAdhocctlChatPacketS2C));

          // Fix RX Buffer Length
          rxpos -= sizeof(SceNetAdhocctlChatPacketS2C);
        }
      }

      // Connect Packet
      else if(rx[0] == OPCODE_CONNECT) {
        // Enough Data available
        if(rxpos >= sizeof(SceNetAdhocctlConnectPacketS2C)) {
          // Log Incoming Peer
          INFO_LOG(SCENET,"Incoming Peer Data...\n");

          // Cast Packet
          SceNetAdhocctlConnectPacketS2C * packet = (SceNetAdhocctlConnectPacketS2C *)rx;

          // Add User
          __addFriend(packet);

          // Update HUD User Count
#ifdef LOCALHOST_AS_PEER
          setUserCount(_getActivePeerCount());
#else
          //setUserCount(_getActivePeerCount()+1);
#endif

          // Move RX Buffer
          memmove(rx, rx + sizeof(SceNetAdhocctlConnectPacketS2C), sizeof(rx) - sizeof(SceNetAdhocctlConnectPacketS2C));

          // Fix RX Buffer Length
          rxpos -= sizeof(SceNetAdhocctlConnectPacketS2C);
        }
      }

      // Disconnect Packet
      else if(rx[0] == OPCODE_DISCONNECT) {
        // Enough Data available
        if(rxpos >= sizeof(SceNetAdhocctlDisconnectPacketS2C)) {
          // Log Incoming Peer Delete Request
          INFO_LOG(SCENET,"FriendFinder: Incoming Peer Data Delete Request...\n");

          // Cast Packet
          SceNetAdhocctlDisconnectPacketS2C * packet = (SceNetAdhocctlDisconnectPacketS2C *)rx;

          // Delete User by IP
          __deleteFriendByIP(packet->ip);

          // Update HUD User Count
#ifdef LOCALHOST_AS_PEER
          setUserCount(_getActivePeerCount());
#else
          //setUserCount(_getActivePeerCount()+1);
#endif

          // Move RX Buffer
          memmove(rx, rx + sizeof(SceNetAdhocctlDisconnectPacketS2C), sizeof(rx) - sizeof(SceNetAdhocctlDisconnectPacketS2C));

          // Fix RX Buffer Length
          rxpos -= sizeof(SceNetAdhocctlDisconnectPacketS2C);
        }
      }

      // Scan Packet
      else if(rx[0] == OPCODE_SCAN) {
        // Enough Data available
        if(rxpos >= sizeof(SceNetAdhocctlScanPacketS2C)) {
          // Log Incoming Network Information
          INFO_LOG(SCENET,"Incoming Group Information...\n");
          // Cast Packet
          SceNetAdhocctlScanPacketS2C * packet = (SceNetAdhocctlScanPacketS2C *)rx;

          // Allocate Structure Data
          SceNetAdhocctlScanInfo * group = (SceNetAdhocctlScanInfo *)malloc(sizeof(SceNetAdhocctlScanInfo));

          // Allocated Structure Data
          if(group != NULL)
          {
            // Clear Memory
            memset(group, 0, sizeof(SceNetAdhocctlScanInfo));

            // Link to existing Groups
            group->next = networks;

            // Copy Group Name
            group->group_name = packet->group;

            // Set Group Host
            group->bssid.mac_addr = packet->mac;

            // Link into Group List
            networks = group;
          }

          // Move RX Buffer
          memmove(rx, rx + sizeof(SceNetAdhocctlScanPacketS2C), sizeof(rx) - sizeof(SceNetAdhocctlScanPacketS2C));

          // Fix RX Buffer Length
          rxpos -= sizeof(SceNetAdhocctlScanPacketS2C);
        }
      }

      // Scan Complete Packet
      else if(rx[0] == OPCODE_SCAN_COMPLETE) {
        // Log Scan Completion
        INFO_LOG(SCENET,"FriendFinder: Incoming Scan complete response...\n");

        // Change State
        threadStatus = ADHOCCTL_STATE_DISCONNECTED;

        // Notify Event Handlers
        CoreTiming::ScheduleEvent_Threadsafe_Immediate(eventHandlerUpdate,join32(ADHOCCTL_EVENT_SCAN, 0));
        //int i = 0; for(; i < ADHOCCTL_MAX_HANDLER; i++)
        //{
        //        // Active Handler
        //        if(_event_handler[i] != NULL) _event_handler[i](ADHOCCTL_EVENT_SCAN, 0, _event_args[i]);
        //}

        // Move RX Buffer
        memmove(rx, rx + 1, sizeof(rx) - 1);

        // Fix RX Buffer Length
        rxpos -= 1;
      }
    }

    //      Delay Thread (10ms)
#ifdef _WIN32
    Sleep(10);
#else
    sleep(10);
#endif
  }

  // Log Shutdown
  INFO_LOG(SCENET, "FriendFinder: End of Friend Finder Thread\n");

  // Return Success
  return 0;
}

/**
 * Check whether Network Name contains only valid symbols
 * @param group_name To-be-checked Network Name
 * @return 1 if valid or... 0
 */
int __validNetworkName(const SceNetAdhocctlGroupName * group_name) {
  // Result
  int valid = 1;

  // Name given
  if(group_name != NULL) {
    // Iterate Name Characters
    int i = 0; for(; i < ADHOCCTL_GROUPNAME_LEN && valid; i++) {
      // End of Name
      if(group_name->data[i] == 0) break;

      // Not a digit
      if(group_name->data[i] < '0' || group_name->data[i] > '9') {
        // Not 'A' to 'Z'
        if(group_name->data[i] < 'A' || group_name->data[i] > 'Z') {
          // Not 'a' to 'z'
          if(group_name->data[i] < 'a' || group_name->data[i] > 'z') {
            // Invalid Name
            valid = 0;
          }
        }
      }
    }
  }

  // Return Result
  return valid;
}

/**
 * Local MAC Check
 * @param saddr To-be-checked MAC Address
 * @return 1 if valid or... 0
 */
int _IsLocalMAC(const SceNetEtherAddr * addr) {
  // Get Local MAC Address
  SceNetEtherAddr saddr;
  __getLocalMac(&saddr);
  //sceNetGetLocalEtherAddr(&saddr);

  // Compare MAC Addresses
  int match = memcmp((const void *)addr, (const void *)&saddr, ETHER_ADDR_LEN);

  // Return Result
  return (match == 0);
}

/**
 * Resolve IP to MAC
 * @param ip Peer IP Address
 * @param mac OUT: Peer MAC
 * @return 0 on success or... ADHOC_NO_ENTRY
 */
int __resolveIP(uint32_t ip, SceNetEtherAddr * mac) {
  sockaddr_in addr;
  __getLocalIp(&addr);
  uint32 localIp = addr.sin_addr.s_addr;
  if(ip == localIp){
    __getLocalMac(mac);
    return 0;
  }

  // Multithreading Lock
  peerlock.lock();

  // Peer Reference
  SceNetAdhocctlPeerInfo * peer = friends;

  // Iterate Peers
  for(; peer != NULL; peer = peer->next) {
    // Found Matching Peer
    if(peer->ip_addr == ip) {
      // Copy Data
      *mac = peer->mac_addr;

      // Multithreading Unlock
      peerlock.unlock();

      // Return Success
      return 0;
    }
  }

  // Multithreading Unlock
  peerlock.unlock();

  // Peer not found
  return ERROR_NET_ADHOC_NO_ENTRY;
}
/**
 * Resolve MAC to IP
 * @param mac Peer MAC Address
 * @param ip OUT: Peer IP
 * @return 0 on success or... ADHOC_NO_ENTRY
 */
int __resolveMAC(SceNetEtherAddr * mac, uint32_t * ip) {
  // Get Local MAC Address
  SceNetEtherAddr localMac;
  __getLocalMac(&localMac);
  // Local MAC Requested
  if(memcmp(&localMac, mac, sizeof(SceNetEtherAddr)) == 0) {
    // Get Local IP Address
    sockaddr_in sockAddr;
    __getLocalIp(&sockAddr);
    *ip = sockAddr.sin_addr.s_addr;
    return 0; // return succes
  }

  // Multithreading Lock
  peerlock.lock();

  // Peer Reference
  SceNetAdhocctlPeerInfo * peer = friends;

  // Iterate Peers
  for(; peer != NULL; peer = peer->next) {
    // Found Matching Peer
    if(memcmp(&peer->mac_addr, mac, sizeof(SceNetEtherAddr)) == 0) {
      // Copy Data
      *ip = peer->ip_addr;

      // Multithreading Unlock
      peerlock.unlock();

      // Return Success
      return 0;
    }
  }

  // Multithreading Unlock
  peerlock.unlock();

  // Peer not found
  return ERROR_NET_ADHOC_NO_ENTRY;
}
/**
 * PDP Port Check
 * @param port To-be-checked Port
 * @return 1 if in use or... 0
 */
int _IsPDPPortInUse(uint16_t port) {
  // Iterate Elements
  int i = 0; for(; i < 255; i++) if(pdp[i] != NULL && pdp[i]->lport == port) return 1;

  // Unused Port
  return 0;
}

/**
 * Broadcast MAC Check
 * @param addr To-be-checked MAC Address
 * @return 1 if Broadcast MAC or... 0
 */
int __isBroadcastMAC(const SceNetEtherAddr * addr) {
  // Broadcast MAC
  if(memcmp(addr->data, "\xFF\xFF\xFF\xFF\xFF\xFF", ETHER_ADDR_LEN) == 0) return 1;
  // Normal MAC
  return 0;
}

/**
 * Closes & Deletes all PDP Sockets
 */
void __deleteAllPDP(void) {
  // Iterate Element
  int i = 0; for(; i < 255; i++) {
    // Active Socket
    if(pdp[i] != NULL) {
      // Close Socket
      closesocket(pdp[i]->id);

      // Free Memory
      free(pdp[i]);

      // Delete Reference
      pdp[i] = NULL;
    }
  }
}

/**
 * Closes & Deletes all PTP Sockets
 */
void __deleteAllPTP(void) {
  // Iterate Element
  int i = 0; for(; i < 255; i++) {
    // Active Socket
    if(ptp[i] != NULL) {
      // Close Socket
      closesocket(ptp[i]->id);

      // Free Memory
      free(ptp[i]);

      // Delete Reference
      ptp[i] = NULL;
    }
  }
}

void __handlerUpdateCallback(u64 userdata, int cycleslate){
  int buff[2];
  split64(userdata,buff);
  __UpdateAdhocctlHandlers(buff[0], buff[1]);
}

u32 sceNetAdhocInit() {
  // Library uninitialized
  ERROR_LOG(SCENET, "UNIMPL sceNetAdhocInit()");
  if(!netAdhocInited) {
    // Clear Translator Memory
    memset(&pdp, 0, sizeof(pdp));
    memset(&ptp, 0, sizeof(ptp));

    // Library initialized
    netAdhocInited = true;

    // Return Success
    return 0;
  }
  // Already initialized
  return ERROR_NET_ADHOC_ALREADY_INITIALIZED;
}

u32 sceNetAdhocctlInit(int stackSize, int prio, u32 productAddr) {
  ERROR_LOG(SCENET, "UNIMPL sceNetAdhocctlInit(%i, %i, %08x)", stackSize, prio, productAddr);
  if (netAdhocctlInited) {
    return ERROR_NET_ADHOCCTL_ALREADY_INITIALIZED;
  }else{
    if(__init_network((SceNetAdhocctlAdhocId *)Memory::GetPointer(productAddr)) == 0){
      netAdhocctlInited = true;
      eventHandlerUpdate = CoreTiming::RegisterEvent("HandlerUpdateEvent", __handlerUpdateCallback);
      friendFinderThread = std::thread(__friendFinder);
    }else{
      return -1; // Generic error
    }
  }
  return 0;
}



// Seems to always return 0, and write 0 to the pointer..
// TODO: Eventually research what possible states there are
int sceNetAdhocctlGetState(u32 ptrToStatus) {
  WARN_LOG(SCENET, "UNTESTED sceNetAdhocctlGetState(%x)", ptrToStatus);
  if (Memory::IsValidAddress(ptrToStatus))
    Memory::Write_U32(0, ptrToStatus);
  else
    ERROR_LOG(SCENET, "UNTESTED sceNetAdhocctlGetState(%x): Tried to write invalid location", ptrToStatus);

  return 0;
}

/**
 * Adhoc Emulator PDP Socket Creator
 * @param saddr Local MAC (Unused)
 * @param sport Local Binding Port
 * @param bufsize Socket Buffer Size
 * @param flag Bitflags (Unused)
 * @return Socket ID > 0 on success or... ADHOC_NOT_INITIALIZED, ADHOC_INVALID_ARG, ADHOC_SOCKET_ID_NOT_AVAIL, ADHOC_INVALID_ADDR, ADHOC_PORT_NOT_AVAIL, ADHOC_INVALID_PORT, ADHOC_PORT_IN_USE, NET_NO_SPACE
 */
int sceNetAdhocPdpCreate(const char *mac, u32 port, int bufferSize, u32 unknown) {
  ERROR_LOG(SCENET, "UNIMPL sceNetAdhocPdpCreate(%s, %x, %x, %x)", mac, port, bufferSize, unknown);
  // Library is initialized
  SceNetEtherAddr * saddr = (SceNetEtherAddr *)mac;
  if(netAdhocInited) {
    // Valid Arguments are supplied
    if(mac != NULL && bufferSize > 0) {
      // Valid MAC supplied
      if(_IsLocalMAC(saddr)) {
        //// Unused Port supplied
        //if(!_IsPDPPortInUse(port)) {} 
        //
        //// Port is in use by another PDP Socket
        //return ERROR_NET_ADHOC_PORT_IN_USE;

        // Create Internet UDP Socket
        int usocket = INVALID_SOCKET;
        usocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        // Valid Socket produced
        if(usocket != INVALID_SOCKET) {
          // Enable Port Re-use
          //setsockopt(usocket, SOL_SOCKET, SO_REUSEADDR, &_one, sizeof(_one)); NO idea if we need this
          // Binding Information for local Port
          sockaddr_in addr;
          addr.sin_family = AF_INET;
          addr.sin_addr.s_addr = INADDR_ANY;

          addr.sin_port = port; //This not safe in any way...

          // Bound Socket to local Port
          if(bind(usocket, (sockaddr *)&addr, sizeof(addr)) == 0) {
            // Allocate Memory for Internal Data
            SceNetAdhocPdpStat * internal = (SceNetAdhocPdpStat *)malloc(sizeof(SceNetAdhocPdpStat));

            // Allocated Memory
            if(internal != NULL) {
              // Clear Memory
              memset(internal, 0, sizeof(SceNetAdhocPdpStat));

              // Find Free Translator Index
              int i = 0; for(; i < 255; i++) if(pdp[i] == NULL) break;

              // Found Free Translator Index
              if(i < 255) {
                // Fill in Data
                internal->id = usocket;
                internal->laddr = *saddr;
                internal->lport = port;
                internal->rcv_sb_cc = bufferSize;

                // Link Socket to Translator ID
                pdp[i] = internal;

                // Forward Port on Router
                //sceNetPortOpen("UDP", sport); // I need to figure out how to use this in windows/linux

                // Success
                return i + 1;
              }

              // Free Memory for Internal Data
              free(internal);
            }
          }

          // Close Socket
          closesocket(usocket);
        }

        // Default to No-Space Error
        return ERROR_NET_NO_SPACE;

      }

      // Invalid MAC supplied
      return ERROR_NET_ADHOC_INVALID_ADDR;
    }

    // Invalid Arguments were supplied
    return ERROR_NET_ADHOC_INVALID_ARG;
  }

  // Library is uninitialized
  return ERROR_NET_ADHOC_NOT_INITIALIZED;
}

// TODO: Should we really write the struct if we're disconnected?
int sceNetAdhocctlGetParameter(u32 paramAddr) {
  WARN_LOG(SCENET, "UNTESTED sceNetAdhocctlGetParameter(%x)", paramAddr);
  struct SceNetAdhocctlParams params;
  params.channel = 0;
  for (int i = 0; i < 6; i++)
    params.bssid[i] = i + 1;
  strcpy(params.name, "");
  strcpy(params.nickname, "");

  if (Memory::IsValidAddress(paramAddr))
    Memory::WriteStruct(paramAddr, &params);

  return ERROR_NET_ADHOCCTL_DISCONNECTED;
}

/**
 * Adhoc Emulator PDP Send Call
 * @param id Socket File Descriptor
 * @param daddr Target MAC Address
 * @param dport Target Port
 * @param data Data Payload
 * @param len Payload Length
 * @param timeout Send Timeout
 * @param flag Nonblocking Flag
 * @return 0 on success or... ADHOC_INVALID_ARG, ADHOC_NOT_INITIALIZED, ADHOC_INVALID_SOCKET_ID, ADHOC_SOCKET_DELETED, ADHOC_INVALID_ADDR, ADHOC_INVALID_PORT, ADHOC_INVALID_DATALEN, ADHOC_SOCKET_ALERTED, ADHOC_TIMEOUT, ADHOC_THREAD_ABORTED, ADHOC_WOULD_BLOCK, NET_NO_SPACE, NET_INTERNAL
 */
int sceNetAdhocPdpSend(int id, const char *mac, u32 port, void *data, int len, int timeout, int flag) {
  ERROR_LOG(SCENET, "UNIMPL sceNetAdhocPdpSend(%d, %s, %d, %p, %p, %d, %d)", id, mac, port, data, len, timeout, flag);
  SceNetEtherAddr * daddr = (SceNetEtherAddr *)mac;
  uint16 dport = (uint16 )port;
  // Library is initialized
  if(netAdhocInited) {
    // Valid Port
    if(dport != 0) {
      // Valid Data Length
      if(len > 0) {
        // Valid Socket ID
        if(id > 0 && id <= 255 && pdp[id - 1] != NULL) {
          // Cast Socket
          SceNetAdhocPdpStat * socket = pdp[id - 1];

          // Valid Data Buffer
          if(data != NULL) {
            // Valid Destination Address
            if(daddr != NULL) {
              // Log Destination
// #ifdef DEBUG
//               printk("Attempting PDP Send to %02X:%02X:%02X:%02X:%02X:%02X on Port %u\n", daddr->data[0], daddr->data[1], daddr->data[2], daddr->data[3], daddr->data[4], daddr->data[5], dport);
// #endif

              // Schedule Timeout Removal
              if(flag) timeout = 0;

              // Apply Send Timeout Settings to Socket
              setsockopt(socket->id, SOL_SOCKET, SO_SNDTIMEO, (const char *)&timeout, sizeof(timeout));

              // Single Target
              if(!__isBroadcastMAC(daddr))
              {
                // Fill in Target Structure
                sockaddr_in target;
                target.sin_family = AF_INET;
                target.sin_port = htons(dport);

                // Get Peer IP
                if(__resolveMAC((SceNetEtherAddr *)daddr, &target.sin_addr.s_addr) == 0) {
                  // Acquire Network Lock
                  //_acquireNetworkLock();

                  // Send Data
                  __change_blocking_mode(socket->id, flag);
                  int sent = sendto(socket->id, (const char *)data, len, 0, (sockaddr *)&target, sizeof(target));
                  __change_blocking_mode(socket->id, 0);

                  // Free Network Lock
                  //_freeNetworkLock();

                  // Sent Data
                  if(sent == len) {
                    // Success
                    return 0;
                  }

                  // Blocking Situation
                  if(flag) return ERROR_NET_ADHOC_WOULD_BLOCK;

                  // Timeout
                  return ERROR_NET_ADHOC_TIMEOUT;
                }
              }

              // Broadcast Target
              else {
                // Acquire Network Lock
                //_acquireNetworkLock();

#ifdef BROADCAST_TO_LOCALHOST
                //// Get Local IP Address
                //union SceNetApctlInfo info; if(sceNetApctlGetInfo(PSP_NET_APCTL_INFO_IP, &info) == 0) {
                //	// Fill in Target Structure
                //	SceNetInetSockaddrIn target;
                //	target.sin_family = AF_INET;
                //	sceNetInetInetAton(info.ip, &target.sin_addr);
                //	target.sin_port = sceNetHtons(dport);
                //	
                //	// Send Data
                //	sceNetInetSendto(socket->id, data, len, ((flag != 0) ? (INET_MSG_DONTWAIT) : (0)), (SceNetInetSockaddr *)&target, sizeof(target));
                //}
#endif

                // Acquire Peer Lock
                peerlock.lock();

                // Iterate Peers
                SceNetAdhocctlPeerInfo * peer = friends;
                for(; peer != NULL; peer = peer->next) {
                  // Fill in Target Structure
                  sockaddr_in target;
                  target.sin_family = AF_INET;
                  target.sin_addr.s_addr = peer->ip_addr;
                  target.sin_port = htons(dport);

                  // Send Data
                  sendto(socket->id, (const char *)data, len, 0, (sockaddr *)&target, sizeof(target));
                }

                // Free Peer Lock
                peerlock.unlock();

                // Free Network Lock
                //_freeNetworkLock();

                // Broadcast never fails!
                return 0;
              }
            }

            // Invalid Destination Address
            return ERROR_NET_ADHOC_INVALID_ADDR;
          }

          // Invalid Argument
          return ERROR_NET_ADHOC_INVALID_ARG;
        }

        // Invalid Socket ID
        return ERROR_NET_ADHOC_INVALID_SOCKET_ID;
      }

      // Invalid Data Length
      return ERROR_NET_ADHOC_INVALID_DATALEN;
    }

    // Invalid Destination Port
    return ERROR_NET_ADHOC_INVALID_PORT;
  }

  // Library is uninitialized
  return ERROR_NET_ADHOC_NOT_INITIALIZED;

}

/**
 * Adhoc Emulator PDP Receive Call
 * @param id Socket File Descriptor
 * @param saddr OUT: Source MAC Address
 * @param sport OUT: Source Port
 * @param buf OUT: Received Data
 * @param len IN: Buffer Size OUT: Received Data Length
 * @param timeout Receive Timeout
 * @param flag Nonblocking Flag
 * @return 0 on success or... ADHOC_INVALID_ARG, ADHOC_NOT_INITIALIZED, ADHOC_INVALID_SOCKET_ID, ADHOC_SOCKET_DELETED, ADHOC_SOCKET_ALERTED, ADHOC_WOULD_BLOCK, ADHOC_TIMEOUT, ADHOC_NOT_ENOUGH_SPACE, ADHOC_THREAD_ABORTED, NET_INTERNAL
 */
int sceNetAdhocPdpRecv(int id, void *addr, void * port, void *buf, void *dataLength, u32 timeout, int flag) {
  //ERROR_LOG(SCENET, "UNIMPL sceNetAdhocPdpRecv(%d, %s, %d, %p, %p, %d, %d)", id, addr, port, buf, dataLength, timeout, flag);
  // This spams a lot...
  SceNetEtherAddr *saddr = (SceNetEtherAddr *)addr;
  uint16_t * sport = (uint16_t *)port;
  int * len = (int *)dataLength;
  if(netAdhocInited) {
    // Valid Socket ID
    if(id > 0 && id <= 255 && pdp[id - 1] != NULL) {
      // Cast Socket
      SceNetAdhocPdpStat * socket = pdp[id - 1];

      // Valid Arguments
      if(saddr != NULL && port != NULL && buf != NULL && len != NULL && *len > 0) {
#ifndef PDP_DIRTY_MAGIC
        // Schedule Timeout Removal
        if(flag == 1) timeout = 0;
#else
        // Nonblocking Simulator
        int wouldblock = 0;

        // Minimum Timeout
        uint32_t mintimeout = 250000;

        // Nonblocking Call
        if(flag == 1) {
          // Erase Nonblocking Flag
          flag = 0;

          // Set Wouldblock Behaviour
          wouldblock = 1;

          // Set Minimum Timeout (250ms)
          if(timeout < mintimeout) timeout = mintimeout;
        }
#endif

        // Apply Receive Timeout Settings to Socket
        setsockopt(socket->id, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));

        // Sender Address
        sockaddr_in sin;

        // Set Address Length (so we get the sender ip)
        socklen_t sinlen = sizeof(sin);
        //sin.sin_len = (uint8_t)sinlen;
        // Acquire Network Lock
        //_acquireNetworkLock();

        // Receive Data
        __change_blocking_mode(socket->id,flag);
        int received = recvfrom(socket->id, (char *)buf, *len,0,(sockaddr *)&sin, &sinlen);
        __change_blocking_mode(socket->id,0);

        // Received Data
        if(received > 0) {
          // Peer MAC
          SceNetEtherAddr mac;

          // Find Peer MAC
          if(__resolveIP(sin.sin_addr.s_addr, &mac) == 0) {
            // Provide Sender Information
            *saddr = mac;
            *sport = htons(sin.sin_port);

            // Save Length
            *len = received;

            // Free Network Lock
            //_freeNetworkLock();

            // Return Success
            return 0;
          }
        }

        // Free Network Lock
        //_freeNetworkLock();

#ifdef PDP_DIRTY_MAGIC
        // Restore Nonblocking Flag for Return Value
        if(wouldblock) flag = 1;
#endif

        // Nothing received
        if(flag) return ERROR_NET_ADHOC_WOULD_BLOCK;
        return ERROR_NET_ADHOC_TIMEOUT;
      }

      // Invalid Argument
      return ERROR_NET_ADHOC_INVALID_ARG;
    }

    // Invalid Socket ID
    return ERROR_NET_ADHOC_INVALID_SOCKET_ID;
  }

  // Library is uninitialized
  return ERROR_NET_ADHOC_NOT_INITIALIZED;
}

// Assuming < 0 for failure, homebrew SDK doesn't have much to say about this one..
int sceNetAdhocSetSocketAlert(int id, int flag) {
  ERROR_LOG(SCENET, "UNIMPL sceNetAdhocSetSocketAlert(%d, %d)", id, flag);
  return -1;
}

int sceNetAdhocPollSocket(u32 socketStructAddr, int count, int timeout, int nonblock) {
  ERROR_LOG(SCENET, "UNIMPL sceNetAdhocPollSocket(%08x, %i, %i, %i)", socketStructAddr, count, timeout, nonblock);
  return -1;
}

/**
 * Adhoc Emulator PDP Socket Delete
 * @param id Socket File Descriptor
 * @param flag Bitflags (Unused)
 * @return 0 on success or... ADHOC_INVALID_ARG, ADHOC_NOT_INITIALIZED, ADHOC_INVALID_SOCKET_ID
 */
int sceNetAdhocPdpDelete(int id, int unknown) {
  ERROR_LOG(SCENET, "UNIMPL sceNetAdhocPdpDelete(%d, %d)", id, unknown);
  // Library is initialized
  if(netAdhocInited) {
    // Valid Arguments
    if(id > 0 && id <= 255) {
      // Cast Socket
      SceNetAdhocPdpStat * sock = pdp[id - 1];

      // Valid Socket
      if(socket != NULL) {
        // Close Connection
        closesocket(sock->id);

        // Remove Port Forward from Router
        //sceNetPortClose("UDP", socket->lport);

        // Free Memory
        // free(socket);

        // Free Translation Slot
        pdp[id - 1] = NULL;

        // Success
        return 0;
      }

      // Invalid Socket ID
      return ERROR_NET_ADHOC_INVALID_SOCKET_ID;
    }

    // Invalid Argument
    return ERROR_NET_ADHOC_INVALID_ARG;
  }

  // Library is uninitialized
  return ERROR_NET_ADHOC_NOT_INITIALIZED;
}

int sceNetAdhocctlGetAdhocId(u32 productStructAddr) {
  ERROR_LOG(SCENET, "UNIMPL sceNetAdhocctlGetAdhocId(%x)", productStructAddr);
  return 0;
}

int sceNetAdhocctlScan() {
  ERROR_LOG(SCENET, "UNIMPL sceNetAdhocctlScan()");
  __UpdateAdhocctlHandlers(0, ERROR_NET_ADHOCCTL_WLAN_SWITCH_OFF);

  return 0;
}

int sceNetAdhocctlGetScanInfo() {
  ERROR_LOG(SCENET, "UNIMPL sceNetAdhocctlGetScanInfo()");
  return 0;
}

// TODO: How many handlers can the PSP actually have for Adhocctl?
// TODO: Should we allow the same handler to be added more than once?
u32 sceNetAdhocctlAddHandler(u32 handlerPtr, u32 handlerArg) {
  bool foundHandler = false;
  u32 retval = 0;
  struct AdhocctlHandler handler;
  memset(&handler, 0, sizeof(handler));

  while (adhocctlHandlers.find(retval) != adhocctlHandlers.end())
    ++retval;

  handler.entryPoint = handlerPtr;
  handler.argument = handlerArg;

  for (std::map<int, AdhocctlHandler>::iterator it = adhocctlHandlers.begin(); it != adhocctlHandlers.end(); it++) {
    if (it->second.entryPoint == handlerPtr) {
      foundHandler = true;
      break;
    }
  }

  if (!foundHandler && Memory::IsValidAddress(handlerPtr)) {
    if (adhocctlHandlers.size() >= MAX_ADHOCCTL_HANDLERS) {
      ERROR_LOG(SCENET, "UNTESTED UNTESTED sceNetAdhocctlAddHandler(%x, %x): Too many handlers", handlerPtr, handlerArg);
      retval = ERROR_NET_ADHOCCTL_TOO_MANY_HANDLERS;
      return retval;
    }
    adhocctlHandlers[retval] = handler;
    WARN_LOG(SCENET, "UNTESTED sceNetAdhocctlAddHandler(%x, %x): added handler %d", handlerPtr, handlerArg, retval);
  } else {
    ERROR_LOG(SCENET, "UNTESTED sceNetAdhocctlAddHandler(%x, %x): Same handler already exists", handlerPtr, handlerArg);
  }

  // The id to return is the number of handlers currently registered
  return retval;
}


u32 sceNetAdhocctlDisconnect() {
  // Library initialized
  if(netAdhocctlInited) {
    // Connected State (Adhoc Mode)
    if(threadStatus == ADHOCCTL_STATE_CONNECTED) {
      // Clear Network Name
      memset(&parameter.group_name, 0, sizeof(parameter.group_name));

      // Set Disconnected State
      threadStatus = ADHOCCTL_STATE_DISCONNECTED;

      // Set HUD Connection Status
      //setConnectionStatus(0);

      // Prepare Packet
      uint8_t opcode = OPCODE_DISCONNECT;

      // Acquire Network Lock
      //_acquireNetworkLock();

      // Send Disconnect Request Packet
      send(metasocket, (const char *)&opcode, 1, 0);

      // Free Network Lock
      //_freeNetworkLock();

      // Multithreading Lock
      peerlock.lock();

      // Clear Peer List
      __freeFriendsRecursive(friends);

      // Delete Peer Reference
      friends = NULL;

      // Multithreading Unlock
      peerlock.unlock();
    }

    // Notify Event Handlers (even if we weren't connected, not doing this will freeze games like God Eater, which expect this behaviour)
    __UpdateAdhocctlHandlers(ADHOCCTL_EVENT_DISCONNECT,0);

    // Return Success
    return 0;
  }

  // Library uninitialized
  return ERROR_NET_ADHOC_NOT_INITIALIZED;
}


u32 sceNetAdhocctlDelHandler(u32 handlerID) {
  if (adhocctlHandlers.find(handlerID) != adhocctlHandlers.end()) {
    adhocctlHandlers.erase(handlerID);
    WARN_LOG(SCENET, "UNTESTED sceNetAdhocctlDelHandler(%d): deleted handler %d", handlerID, handlerID);
  } else {
    ERROR_LOG(SCENET, "UNTESTED sceNetAdhocctlDelHandler(%d): asked to delete invalid handler %d", handlerID, handlerID);
  }

  return 0;
}

int sceNetAdhocctlTerm() {
  ERROR_LOG(SCENET, "UNIMPL sceNetAdhocctlTerm()");
  if(netAdhocInited){
    netAdhocctlInited = false;
    friendFinderThread.join();
    // Free sttuf here
    closesocket(metasocket);
    metasocket = -1;

#ifdef _WIN32
    WSACleanup(); // WINDOWS ONLY!
#endif
  }

  return 0;
}

int sceNetAdhocctlGetNameByAddr(const char *mac, u32 nameAddr) {
  ERROR_LOG(SCENET, "UNIMPL sceNetAdhocctlGetNameByAddr(%s, %08x)", mac, nameAddr);
  return -1;
}

int sceNetAdhocctlJoin(u32 scanInfoAddr) {
  ERROR_LOG(SCENET, "UNIMPL sceNetAdhocctlJoin(%08x)", scanInfoAddr);
  return -1;
}

int sceNetAdhocctlGetPeerInfo(const char *mac, int size, u32 peerInfoAddr) {
  ERROR_LOG(SCENET, "UNIMPL sceNetAdhocctlGetPeerInfo(%s, %i, %08x)", mac, size, peerInfoAddr);
  return -1;
}

/**
 * Create and / or Join a Virtual Network of the specified Name
 * @param group_name Virtual Network Name
 * @return 0 on success or... ADHOCCTL_NOT_INITIALIZED, ADHOCCTL_INVALID_ARG, ADHOCCTL_BUSY
 */
int sceNetAdhocctlCreate(const char *groupName) {
  ERROR_LOG(SCENET, "UNIMPL sceNetAdhocctlCreate(%s)", groupName);
  const SceNetAdhocctlGroupName * groupNameStruct = (const SceNetAdhocctlGroupName *)groupName;
  // Library initialized
  if(netAdhocctlInited) {
    // Valid Argument
    if(__validNetworkName(groupNameStruct)) {
      // Disconnected State
      if(threadStatus == ADHOCCTL_STATE_DISCONNECTED) {
        // Set Network Name
        if(groupNameStruct != NULL) parameter.group_name = *groupNameStruct;

        // Reset Network Name
        else memset(&parameter.group_name, 0, sizeof(parameter.group_name));

        // Prepare Connect Packet
        SceNetAdhocctlConnectPacketC2S packet;

        // Clear Packet Memory
        memset(&packet, 0, sizeof(packet));

        // Set Packet Opcode
        packet.base.opcode = OPCODE_CONNECT;

        // Set Target Group
        if(groupNameStruct != NULL) packet.group = *groupNameStruct;

        // Acquire Network Lock

        // Send Packet
        send(metasocket, (const char *)&packet, sizeof(packet), 0);

        // Free Network Lock

        // Set HUD Connection Status
        //setConnectionStatus(1);

        // Return Success
        return 0;
      }

      // Connected State
      return ERROR_NET_ADHOCCTL_BUSY;
    }

    // Invalid Argument
    return ERROR_NET_ADHOC_INVALID_ARG;
  }
  // Library uninitialized
  return ERROR_NET_ADHOCCTL_NOT_INITIALIZED;
}

int sceNetAdhocctlConnect(u32 ptrToGroupName) {
  if (Memory::IsValidAddress(ptrToGroupName)) {
    ERROR_LOG(SCENET, "UNIMPL sceNetAdhocctlConnect(groupName=%s)", Memory::GetCharPointer(ptrToGroupName));
    return sceNetAdhocctlCreate(Memory::GetCharPointer(ptrToGroupName));
  } else {
    return ERROR_NET_ADHOC_INVALID_ADDR;
  }
}

int sceNetAdhocctlCreateEnterGameMode(const char *groupName, int unknown, int playerNum, u32 macsAddr, int timeout, int unknown2) {
  ERROR_LOG(SCENET, "UNIMPL sceNetAdhocctlCreateEnterGameMode(%s, %i, %i, %08x, %i, %i)", groupName, unknown, playerNum, macsAddr, timeout, unknown2);
  return -1;
}

int sceNetAdhocctlJoinEnterGameMode(const char *groupName, const char *macAddr, int timeout, int unknown2) {
  ERROR_LOG(SCENET, "UNIMPL sceNetAdhocctlJoinEnterGameMode(%s, %s, %i, %i)", groupName, macAddr, timeout, unknown2);
  return -1;
}

int sceNetAdhocTerm() {
  ERROR_LOG(SCENET, "UNIMPL sceNetAdhocTerm()");
  // Library is initialized
  if(netAdhocInited) {
    // Delete PDP Sockets
    __deleteAllPDP();

    // Delete PTP Sockets
    __deleteAllPTP();

    // Delete Gamemode Buffer
    //_deleteAllGMB();

    // Terminate Internet Library
    //sceNetInetTerm();

    // Unload Internet Modules (Just keep it in memory... unloading crashes?!)
    // if(_manage_modules != 0) sceUtilityUnloadModule(PSP_MODULE_NET_INET);
    // Library shutdown
    netAdhocInited = false;
    return 0;
  }else {
    // Seems to return this when called a second time after being terminated without another initialisation
    return SCE_KERNEL_ERROR_LWMUTEX_NOT_FOUND;
  }
}

int sceNetAdhocGetPdpStat(int structSize, u32 structAddr) {
  ERROR_LOG(SCENET, "UNIMPL sceNetAdhocGetPdpStat(%i, %08x)", structSize, structAddr);
  return 0;
}

int sceNetAdhocGetPtpStat(int structSize, u32 structAddr) {
  ERROR_LOG(SCENET, "UNIMPL sceNetAdhocGetPtpStat(%i, %08x)", structSize, structAddr);
  return 0;
}

int sceNetAdhocPtpOpen(const char *srcmac, int srcport, const char *dstmac, int dstport, int bufsize, int retryDelay, int retryCount, int unknown) {
  ERROR_LOG(SCENET, "UNIMPL sceNetAdhocPtpOpen(%s : %i, %s : %i, %i, %i, %i, %08x)", srcmac, srcport, dstmac, dstport, bufsize, retryDelay, retryCount, unknown);
  return 0;
}

int sceNetAdhocPtpAccept(int id, u32 peerMacAddrPtr, u32 peerPortPtr, int timeout, int nonblock) {
  ERROR_LOG(SCENET, "UNIMPL sceNetAdhocPtpAccept(%i, %08x, %08x, %i, %i)", id, peerMacAddrPtr, peerPortPtr, timeout, nonblock);
  return 0;
}

int sceNetAdhocPtpConnect(int id, int timeout, int nonblock) {
  ERROR_LOG(SCENET, "UNIMPL sceNetAdhocPtpConnect(%i, %i, %i)", id, timeout, nonblock);
  return -1;
}

int sceNetAdhocPtpClose(int id, int unknown) {
  ERROR_LOG(SCENET, "UNIMPL sceNetAdhocPtpClose(%i, %i)", id, unknown);
  return 0;
}

int sceNetAdhocPtpListen(const char *srcmac, int srcport, int bufsize, int retryDelay, int retryCount, int queue, int unk) {
  ERROR_LOG(SCENET, "UNIMPL sceNetAdhocPtpListen(%s : %i, %i, %i, %i, %i, %i)", srcmac, srcport, bufsize, retryDelay, retryCount, queue, unk);
  return 0;
}

int sceNetAdhocPtpSend(int id, u32 data, u32 dataSizeAddr, int timeout, int nonblock) {
  ERROR_LOG(SCENET, "UNIMPL sceNetAdhocPtpSend(%i, %08x, %08x, %i, %i)", id, data, dataSizeAddr, timeout, nonblock);
  return 0;
}

int sceNetAdhocPtpRecv(int id, u32 data, u32 dataSizeAddr, int timeout, int nonblock) {
  ERROR_LOG(SCENET, "UNIMPL sceNetAdhocPtpRecv(%i, %08x, %08x, %i, %i)", id, data, dataSizeAddr, timeout, nonblock);
  return 0;
}

int sceNetAdhocPtpFlush(int id, int timeout, int nonblock) {
  ERROR_LOG(SCENET, "UNIMPL sceNetAdhocPtpFlush(%i, %i, %i)", id, timeout, nonblock);
  return 0;
}

int sceNetAdhocGameModeCreateMaster(u32 data, int size) {
  ERROR_LOG(SCENET, "UNIMPL sceNetAdhocGameModeCreateMaster(%08x, %i)", data, size);
  return -1;
}

int sceNetAdhocGameModeCreateReplica(const char *mac, u32 data, int size) {
  ERROR_LOG(SCENET, "UNIMPL sceNetAdhocGameModeCreateReplica(%s, %08x, %i)", mac, data, size);
  return -1;
}

int sceNetAdhocGameModeUpdateMaster() {
  ERROR_LOG(SCENET, "UNIMPL sceNetAdhocGameModeUpdateMaster()");
  return -1;
}

int sceNetAdhocGameModeDeleteMaster() {
  ERROR_LOG(SCENET, "UNIMPL sceNetAdhocGameModeDeleteMaster()");
  return -1;
}

int sceNetAdhocGameModeUpdateReplica(int id, u32 infoAddr) {
  ERROR_LOG(SCENET, "UNIMPL sceNetAdhocGameModeUpdateReplica(%i, %08x)", id, infoAddr);
  return -1;
}

int sceNetAdhocGameModeDeleteReplica(int id) {
  ERROR_LOG(SCENET, "UNIMPL sceNetAdhocGameModeDeleteReplica(%i)", id);
  return -1;
}

int sceNetAdhocGetSocketAlert() {
  ERROR_LOG(SCENET, "UNIMPL sceNetAdhocGetSocketAlert()");
  return 0;
}

int sceNetAdhocMatchingInit(u32 memsize) {
  ERROR_LOG(SCENET, "UNIMPL sceNetAdhocMatchingInit(%08x)", memsize);
  if (netAdhocMatchingInited) 
    return ERROR_NET_ADHOC_MATCHING_ALREADY_INITIALIZED;
  netAdhocMatchingInited = true;

  return 0;
}

int sceNetAdhocMatchingTerm() {
  ERROR_LOG(SCENET, "UNIMPL sceNetAdhocMatchingTerm()");
  netAdhocMatchingInited = false;

  return 0;
}



// Presumably returns a "matchingId".
int sceNetAdhocMatchingCreate(int mode, int maxPeers, int port, int bufSize, int helloDelay, int pingDelay, int initCount, int msgDelay, u32 callbackAddr) {
  ERROR_LOG(SCENET, "UNIMPL sceNetAdhocMatchingCreate(%i, %i, %i, %i, %i, %i, %i, %i, %08x)", mode, maxPeers, port, bufSize, helloDelay, pingDelay, initCount, msgDelay, callbackAddr);
  return -1;
}

int sceNetAdhocMatchingStart(int matchingId, int evthPri, int evthStack, int inthPri, int inthStack, int optLen, u32 optDataAddr) {
  ERROR_LOG(SCENET, "UNIMPL sceNetAdhocMatchingStart(%i, %i, %i, %i, %i, %i, %08x)", matchingId, evthPri, evthStack, inthPri, inthStack, optLen, optDataAddr);
  return -1;
}

int sceNetAdhocMatchingStop(int matchingId) {
  ERROR_LOG(SCENET, "UNIMPL sceNetAdhocMatchingStop(%i)", matchingId);
  return -1;
}

int sceNetAdhocMatchingDelete(int matchingId) {
  ERROR_LOG(SCENET, "UNIMPL sceNetAdhocMatchingDelete(%i)", matchingId);
  return -1;
}

int sceNetAdhocMatchingSelectTarget(int matchingId, const char *macAddress, int optLen, u32 optDataPtr) {
  ERROR_LOG(SCENET, "UNIMPL sceNetAdhocMatchingSelectTarget(%i, %s, %i, %08x)", matchingId, macAddress, optLen, optDataPtr);
  return -1;
}

int sceNetAdhocMatchingCancelTargetWithOpt(int matchingId, const char *macAddress, int optLen, u32 optDataPtr) {
  ERROR_LOG(SCENET, "UNIMPL sceNetAdhocMatchingCancelTargetWithOpt(%i, %s, %i, %08x)", matchingId, macAddress, optLen, optDataPtr);
  return -1;
}

int sceNetAdhocMatchingCancelTarget(int matchingId, const char *macAddress) {
  ERROR_LOG(SCENET, "UNIMPL sceNetAdhocMatchingCancelTarget(%i, %s)", matchingId, macAddress);
  return -1;
}

int sceNetAdhocMatchingGetHelloOpt(int matchingId, u32 optLenAddr, u32 optDataAddr) {
  ERROR_LOG(SCENET, "UNIMPL sceNetAdhocMatchingGetHelloOpt(%i, %08x, %08x)", matchingId, optLenAddr, optDataAddr);
  return -1;
}

int sceNetAdhocMatchingSetHelloOpt(int matchingId, int optLenAddr, u32 optDataAddr) {
  ERROR_LOG(SCENET, "UNIMPL sceNetAdhocMatchingSetHelloOpt(%i, %i, %08x)", matchingId, optLenAddr, optDataAddr);
  return -1;
}

int sceNetAdhocMatchingGetMembers(int matchingId, u32 sizeAddr, u32 buf) {
  ERROR_LOG(SCENET, "UNIMPL sceNetAdhocMatchingGetMembers(%i, %08x, %08x)", matchingId, sizeAddr, buf);
  return -1;
}

int sceNetAdhocMatchingSendData(int matchingId, const char *mac, int dataLen, u32 dataAddr) {
  ERROR_LOG(SCENET, "UNIMPL sceNetAdhocMatchingSendData(%i, %s, %i, %08x)", matchingId, mac, dataLen, dataAddr);
  return -1;
}

int sceNetAdhocMatchingAbortSendData(int matchingId, const char *mac) {
  ERROR_LOG(SCENET, "UNIMPL sceNetAdhocMatchingAbortSendData(%i, %s)", matchingId, mac);
  return -1;
}

int sceNetAdhocMatchingGetPoolMaxAlloc() {
  ERROR_LOG(SCENET, "UNIMPL sceNetAdhocMatchingGetPoolMaxAlloc()");
  return -1;
}

int sceNetAdhocMatchingGetPoolStat() {
  ERROR_LOG(SCENET, "UNIMPL sceNetAdhocMatchingGetPoolStat()");
  return -1;
}

const HLEFunction sceNetAdhoc[] = {
  {0xE1D621D7, WrapU_V<sceNetAdhocInit>, "sceNetAdhocInit"}, 
  {0xA62C6F57, WrapI_V<sceNetAdhocTerm>, "sceNetAdhocTerm"}, 
  {0x0AD043ED, WrapI_U<sceNetAdhocctlConnect>, "sceNetAdhocctlConnect"},
  {0x6f92741b, WrapI_CUIU<sceNetAdhocPdpCreate>, "sceNetAdhocPdpCreate"},
  {0xabed3790, WrapI_ICUVIII<sceNetAdhocPdpSend>, "sceNetAdhocPdpSend"},
  {0xdfe53e03, WrapI_IVVVVUI<sceNetAdhocPdpRecv>, "sceNetAdhocPdpRecv"},
  {0x7f27bb5e, WrapI_II<sceNetAdhocPdpDelete>, "sceNetAdhocPdpDelete"},
  {0xc7c1fc57, WrapI_IU<sceNetAdhocGetPdpStat>, "sceNetAdhocGetPdpStat"},
  {0x157e6225, WrapI_II<sceNetAdhocPtpClose>, "sceNetAdhocPtpClose"},
  {0x4da4c788, WrapI_IUUII<sceNetAdhocPtpSend>, "sceNetAdhocPtpSend"},
  {0x877f6d66, WrapI_CICIIIII<sceNetAdhocPtpOpen>, "sceNetAdhocPtpOpen"},
  {0x8bea2b3e, WrapI_IUUII<sceNetAdhocPtpRecv>, "sceNetAdhocPtpRecv"},
  {0x9df81198, WrapI_IUUII<sceNetAdhocPtpAccept>, "sceNetAdhocPtpAccept"},
  {0xe08bdac1, WrapI_CIIIIII<sceNetAdhocPtpListen>, "sceNetAdhocPtpListen"},
  {0xfc6fc07b, WrapI_III<sceNetAdhocPtpConnect>, "sceNetAdhocPtpConnect"},
  {0x9ac2eeac, WrapI_III<sceNetAdhocPtpFlush>, "sceNetAdhocPtpFlush"},
  {0xb9685118, WrapI_IU<sceNetAdhocGetPtpStat>, "sceNetAdhocGetPtpStat"},
  {0x3278ab0c, WrapI_CUI<sceNetAdhocGameModeCreateReplica>, "sceNetAdhocGameModeCreateReplica"},
  {0x98c204c8, WrapI_V<sceNetAdhocGameModeUpdateMaster>, "sceNetAdhocGameModeUpdateMaster"}, 
  {0xfa324b4e, WrapI_IU<sceNetAdhocGameModeUpdateReplica>, "sceNetAdhocGameModeUpdateReplica"},
  {0xa0229362, WrapI_V<sceNetAdhocGameModeDeleteMaster>, "sceNetAdhocGameModeDeleteMaster"},
  {0x0b2228e9, WrapI_I<sceNetAdhocGameModeDeleteReplica>, "sceNetAdhocGameModeDeleteReplica"},
  {0x7F75C338, WrapI_UI<sceNetAdhocGameModeCreateMaster>, "sceNetAdhocGameModeCreateMaster"},
  {0x73bfd52d, WrapI_II<sceNetAdhocSetSocketAlert>, "sceNetAdhocSetSocketAlert"},
  {0x4d2ce199, WrapI_V<sceNetAdhocGetSocketAlert>, "sceNetAdhocGetSocketAlert"},
  {0x7a662d6b, WrapI_UIII<sceNetAdhocPollSocket>, "sceNetAdhocPollSocket"},
};							

const HLEFunction sceNetAdhocMatching[] = {
  {0x2a2a1e07, WrapI_U<sceNetAdhocMatchingInit>, "sceNetAdhocMatchingInit"},
  {0x7945ecda, WrapI_V<sceNetAdhocMatchingTerm>, "sceNetAdhocMatchingTerm"},
  {0xca5eda6f, WrapI_IIIIIIIIU<sceNetAdhocMatchingCreate>, "sceNetAdhocMatchingCreate"},
  {0x93ef3843, WrapI_IIIIIIU<sceNetAdhocMatchingStart>, "sceNetAdhocMatchingStart"},
  {0x32b156b3, WrapI_I<sceNetAdhocMatchingStop>, "sceNetAdhocMatchingStop"},
  {0xf16eaf4f, WrapI_I<sceNetAdhocMatchingDelete>, "sceNetAdhocMatchingDelete"},
  {0x5e3d4b79, WrapI_ICIU<sceNetAdhocMatchingSelectTarget>, "sceNetAdhocMatchingSelectTarget"},
  {0xea3c6108, WrapI_IC<sceNetAdhocMatchingCancelTarget>, "sceNetAdhocMatchingCancelTarget"},
  {0x8f58bedf, WrapI_ICIU<sceNetAdhocMatchingCancelTargetWithOpt>, "sceNetAdhocMatchingCancelTargetWithOpt"},
  {0xb5d96c2a, WrapI_IUU<sceNetAdhocMatchingGetHelloOpt>, "sceNetAdhocMatchingGetHelloOpt"},
  {0xb58e61b7, WrapI_IIU<sceNetAdhocMatchingSetHelloOpt>, "sceNetAdhocMatchingSetHelloOpt"},
  {0xc58bcd9e, WrapI_IUU<sceNetAdhocMatchingGetMembers>, "sceNetAdhocMatchingGetMembers"},
  {0xf79472d7, WrapI_ICIU<sceNetAdhocMatchingSendData>, "sceNetAdhocMatchingSendData"},
  {0xec19337d, WrapI_IC<sceNetAdhocMatchingAbortSendData>, "sceNetAdhocMatchingAbortSendData"},
  {0x40F8F435, WrapI_V<sceNetAdhocMatchingGetPoolMaxAlloc>, "sceNetAdhocMatchingGetPoolMaxAlloc"},
  {0x9c5cfb7d, WrapI_V<sceNetAdhocMatchingGetPoolStat>, "sceNetAdhocMatchingGetPoolStat"},
};

int sceNetAdhocctlExitGameMode() {
  ERROR_LOG(SCENET, "UNIMPL sceNetAdhocctlExitGameMode()");
  return -1;
}

int sceNetAdhocctlGetGameModeInfo(u32 infoAddr) {
  ERROR_LOG(SCENET, "UNIMPL sceNetAdhocctlGetGameModeInfo(%08x)", infoAddr);
  return -1;
}

int sceNetAdhocctlGetPeerList(u32 sizeAddr, u32 bufAddr) {
  ERROR_LOG(SCENET, "UNIMPL sceNetAdhocctlGetPeerList(%08x, %08x)", sizeAddr, bufAddr);
  return -1;
}

int sceNetAdhocctlGetAddrByName(const char *nickName, u32 sizeAddr, u32 bufAddr) {
  ERROR_LOG(SCENET, "UNIMPL sceNetAdhocctlGetPeerList(%s, %08x, %08x)", nickName, sizeAddr, bufAddr);
  return -1;
}

const HLEFunction sceNetAdhocctl[] = {
  {0xE26F226E, WrapU_IIU<sceNetAdhocctlInit>, "sceNetAdhocctlInit"},
  {0x9D689E13, WrapI_V<sceNetAdhocctlTerm>, "sceNetAdhocctlTerm"},
  {0x20B317A0, WrapU_UU<sceNetAdhocctlAddHandler>, "sceNetAdhocctlAddHandler"},
  {0x6402490B, WrapU_U<sceNetAdhocctlDelHandler>, "sceNetAdhocctlDelHandler"},
  {0x34401D65, WrapU_V<sceNetAdhocctlDisconnect>, "sceNetAdhocctlDisconnect"},
  {0x0ad043ed, WrapI_U<sceNetAdhocctlConnect>, "sceNetAdhocctlConnect"},
  {0x08fff7a0, WrapI_V<sceNetAdhocctlScan>, "sceNetAdhocctlScan"},
  {0x75ecd386, WrapI_U<sceNetAdhocctlGetState>, "sceNetAdhocctlGetState"},
  {0x8916c003, WrapI_CU<sceNetAdhocctlGetNameByAddr>, "sceNetAdhocctlGetNameByAddr"},
  {0xded9d28e, WrapI_U<sceNetAdhocctlGetParameter>, "sceNetAdhocctlGetParameter"},
  {0x81aee1be, WrapI_V<sceNetAdhocctlGetScanInfo>, "sceNetAdhocctlGetScanInfo"},
  {0x5e7f79c9, WrapI_U<sceNetAdhocctlJoin>, "sceNetAdhocctlJoin"},
  {0x8db83fdc, WrapI_CIU<sceNetAdhocctlGetPeerInfo>, "sceNetAdhocctlGetPeerInfo"},
  {0xec0635c1, WrapI_C<sceNetAdhocctlCreate>, "sceNetAdhocctlCreate"},
  {0xa5c055ce, WrapI_CIIUII<sceNetAdhocctlCreateEnterGameMode>, "sceNetAdhocctlCreateEnterGameMode"},
  {0x1ff89745, WrapI_CCII<sceNetAdhocctlJoinEnterGameMode>, "sceNetAdhocctlJoinEnterGameMode"},
  {0xcf8e084d, WrapI_V<sceNetAdhocctlExitGameMode>, "sceNetAdhocctlExitGameMode"},
  {0xe162cb14, WrapI_UU<sceNetAdhocctlGetPeerList>, "sceNetAdhocctlGetPeerList"},
  {0x362cbe8f, WrapI_U<sceNetAdhocctlGetAdhocId>, "sceNetAdhocctlGetAdhocId"},
  {0x5a014ce0, WrapI_U<sceNetAdhocctlGetGameModeInfo>, "sceNetAdhocctlGetGameModeInfo"},
  {0x99560abe, WrapI_CUU<sceNetAdhocctlGetAddrByName>, "sceNetAdhocctlGetAddrByName"},
  {0xb0b80e80, 0, "sceNetAdhocctlCreateEnterGameModeMin"},  // ??
};

const HLEFunction sceNetAdhocDiscover[] = {
  {0x941B3877, 0, "sceNetAdhocDiscoverInitStart"},
  {0x52DE1B97, 0, "sceNetAdhocDiscoverUpdate"},
  {0x944DDBC6, 0, "sceNetAdhocDiscoverGetStatus"},
  {0xA2246614, 0, "sceNetAdhocDiscoverTerm"},
  {0xF7D13214, 0, "sceNetAdhocDiscoverStop"},
  {0xA423A21B, 0, "sceNetAdhocDiscoverRequestSuspend"},
};

void Register_sceNetAdhoc() {
  RegisterModule("sceNetAdhoc", ARRAY_SIZE(sceNetAdhoc), sceNetAdhoc);
  RegisterModule("sceNetAdhocMatching", ARRAY_SIZE(sceNetAdhocMatching), sceNetAdhocMatching);
  RegisterModule("sceNetAdhocDiscover", ARRAY_SIZE(sceNetAdhocDiscover), sceNetAdhocDiscover);
  RegisterModule("sceNetAdhocctl", ARRAY_SIZE(sceNetAdhocctl), sceNetAdhocctl);
}
