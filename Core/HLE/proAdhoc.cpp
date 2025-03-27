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


// proAdhoc

// This is a direct port of Coldbird's code from http://code.google.com/p/aemu/
// All credit goes to him!

#include "ppsspp_config.h"

#include <algorithm>
#include <mutex>
#include <cstring>

#include "Common/Net/SocketCompat.h"

#include "Common/Data/Text/I18n.h"
#include "Common/Data/Text/Parsers.h"
#include "Common/System/OSD.h"
#include "Common/Thread/ThreadUtil.h"

#include "Common/Serialize/SerializeFuncs.h"
#include "Common/TimeUtil.h"

#include "Core/HLE/sceKernelThread.h"
#include "Core/HLE/sceKernel.h"
#include "Core/HLE/sceKernelMutex.h"
#include "Core/HLE/sceUtility.h"

#include "Core/MemMap.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/HLEHelperThread.h"
#include "Core/Config.h"
#include "Core/CoreTiming.h"
#include "Core/Core.h"
#include "Core/HLE/sceKernelInterrupt.h"
#include "Core/HLE/sceKernelMemory.h"
#include "Core/HLE/sceNetAdhoc.h"
#include "Core/Instance.h"
#include "proAdhoc.h" 

#ifdef _WIN32
#undef errno
#define errno WSAGetLastError()
#endif

#if PPSSPP_PLATFORM(SWITCH) && !defined(INADDR_NONE)
// Missing toolchain define
#define INADDR_NONE 0xFFFFFFFF
#endif

uint16_t portOffset;
uint32_t minSocketTimeoutUS;
uint32_t fakePoolSize = 0;
SceNetMallocStat netAdhocPoolStat = {};
SceNetAdhocMatchingContext * contexts = NULL;
char* dummyPeekBuf64k                 = NULL;
int dummyPeekBuf64kSize               = 65536;
int one                               = 1;
std::atomic<bool> friendFinderRunning(false);
SceNetAdhocctlPeerInfo * friends      = NULL;
SceNetAdhocctlScanInfo * networks     = NULL;
SceNetAdhocctlScanInfo * newnetworks  = NULL;
u64 adhocctlStartTime                 = 0;
bool isAdhocctlNeedLogin              = false;
bool isAdhocctlBusy                   = false;
int adhocctlState                     = ADHOCCTL_STATE_DISCONNECTED;
int adhocctlCurrentMode               = ADHOCCTL_MODE_NONE;
int adhocConnectionType               = ADHOC_CONNECT;

int gameModeSocket                    = (int)INVALID_SOCKET; // UDP/PDP socket? on Master only?
int gameModeBuffSize                  = 0;
u8* gameModeBuffer                    = nullptr;
GameModeArea masterGameModeArea;
std::vector<GameModeArea> replicaGameModeAreas;
std::vector<SceNetEtherAddr> requiredGameModeMacs;
std::vector<SceNetEtherAddr> gameModeMacs;
std::map<SceNetEtherAddr, u16_le> gameModePeerPorts;

int actionAfterAdhocMipsCall;
int actionAfterMatchingMipsCall;

// Broadcast MAC
uint8_t broadcastMAC[ETHER_ADDR_LEN] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

// NOTE: This does not need to be managed by the socket manager - not exposed to the game.
std::atomic<int> metasocket((int)INVALID_SOCKET);

SceNetAdhocctlParameter parameter;
SceNetAdhocctlAdhocId product_code;
std::thread friendFinderThread;
std::recursive_mutex peerlock;
AdhocSocket* adhocSockets[MAX_SOCKET];
bool isOriPort = false;
bool isLocalServer = false;
SockAddrIN4 g_adhocServerIP;
SockAddrIN4 g_localhostIP;
sockaddr LocalIP;
int defaultWlanChannel = PSP_SYSTEMPARAM_ADHOC_CHANNEL_11; // Don't put 0(Auto) here, it needed to be a valid/actual channel number

static std::mutex chatLogLock;
static std::vector<std::string> chatLog;
static int chatMessageGeneration = 0;
static int chatMessageCount = 0;

bool isMacMatch(const SceNetEtherAddr* addr1, const SceNetEtherAddr* addr2) {
	// Ignoring the 1st byte since there are games (ie. Gran Turismo) who tamper with the 1st byte of OUI to change the unicast/multicast bit
	return (memcmp(((const char*)addr1)+1, ((const char*)addr2)+1, ETHER_ADDR_LEN-1) == 0);
}

bool isLocalMAC(const SceNetEtherAddr * addr) {
	SceNetEtherAddr saddr;
	getLocalMac(&saddr);

	return isMacMatch(addr, &saddr);
}

bool isPDPPortInUse(uint16_t port) {
	// Iterate Elements
	for (int i = 0; i < MAX_SOCKET; i++) {
		auto sock = adhocSockets[i];
		if (sock != NULL && sock->type == SOCK_PDP)
			if (sock->data.pdp.lport == port)
				return true;
	}
	// Unused Port
	return false;
}

bool isPTPPortInUse(uint16_t port, bool forListen, SceNetEtherAddr* dstmac, uint16_t dstport) {
	// Iterate Sockets
	for (int i = 0; i < MAX_SOCKET; i++) {
		auto sock = adhocSockets[i];
		if (sock != NULL && sock->type == SOCK_PTP)
			// It's allowed to Listen and Open the same PTP port, But it's not allowed to Listen or Open the same PTP port twice (unless destination mac or port are different).
			if (sock->data.ptp.lport == port &&
			    ((forListen && sock->data.ptp.state == ADHOC_PTP_STATE_LISTEN) ||
			     (!forListen && sock->data.ptp.state != ADHOC_PTP_STATE_LISTEN && 
			      sock->data.ptp.pport == dstport && dstmac != nullptr && isMacMatch(&sock->data.ptp.paddr, dstmac)))) 
			{
				return true;
			}
	}
	// Unused Port
	return false;
}

// Replacement for inet_ntoa since it's getting deprecated
std::string ip2str(in_addr in, bool maskPublicIP) {
	char str[INET_ADDRSTRLEN] = "...";
	u8* ipptr = (u8*)&in;
#ifdef _DEBUG
	maskPublicIP = false;
#endif
	if (maskPublicIP && !isPrivateIP(in.s_addr))
		snprintf(str, sizeof(str), "%u.%u.xx.%u", ipptr[0], ipptr[1], ipptr[3]);
	else
		snprintf(str, sizeof(str), "%u.%u.%u.%u", ipptr[0], ipptr[1], ipptr[2], ipptr[3]);
	return std::string(str);
}

std::string mac2str(const SceNetEtherAddr *mac) {
	char str[18] = ":::::";

	if (mac != NULL) {
		snprintf(str, sizeof(str), "%02x:%02x:%02x:%02x:%02x:%02x", mac->data[0], mac->data[1], mac->data[2], mac->data[3], mac->data[4], mac->data[5]);
	}

	return std::string(str);
}

SceNetAdhocMatchingMemberInternal* addMember(SceNetAdhocMatchingContext * context, SceNetEtherAddr * mac) {
	if (context == NULL || mac == NULL) return NULL;
	
	SceNetAdhocMatchingMemberInternal * peer = findPeer(context, mac);
	// Already existed
	if (peer != NULL) {
		WARN_LOG(Log::sceNet, "Member Peer Already Existed! Updating [%s]", mac2str(mac).c_str());
		peer->state = 0;
		peer->sending = 0;
		peer->lastping = CoreTiming::GetGlobalTimeUsScaled();
	}
	// Member is not added yet
	else {
		peer = (SceNetAdhocMatchingMemberInternal *)malloc(sizeof(SceNetAdhocMatchingMemberInternal));
		if (peer != NULL) {
			memset(peer, 0, sizeof(SceNetAdhocMatchingMemberInternal));
			peer->mac = *mac;
			peer->lastping = CoreTiming::GetGlobalTimeUsScaled();
			peerlock.lock();
			peer->next = context->peerlist;
			context->peerlist = peer;
			peerlock.unlock();
		}
	}
	return peer;
}

void addFriend(SceNetAdhocctlConnectPacketS2C * packet) {
	if (packet == NULL) return;

	// Multithreading Lock
	std::lock_guard<std::recursive_mutex> guard(peerlock);

	SceNetAdhocctlPeerInfo * peer = findFriend(&packet->mac);
	// Already existed
	if (peer != NULL) {
		u32 tmpip = packet->ip;
		WARN_LOG(Log::sceNet, "Friend Peer Already Existed! Updating [%s][%s][%s]", mac2str(&packet->mac).c_str(), ip2str(*(struct in_addr*)&tmpip).c_str(), packet->name.data); //inet_ntoa(*(in_addr*)&packet->ip)
		peer->nickname = packet->name;
		peer->mac_addr = packet->mac;
		peer->ip_addr = packet->ip;
		// Calculate final IP-specific Port Offset
		peer->port_offset = ((isOriPort && !isPrivateIP(peer->ip_addr)) ? 0 : portOffset);
		// Update TimeStamp
		peer->last_recv = CoreTiming::GetGlobalTimeUsScaled();
	}
	else {
		// Allocate Structure
		peer = (SceNetAdhocctlPeerInfo *)malloc(sizeof(SceNetAdhocctlPeerInfo));
		// Allocated Structure
		if (peer != NULL) {
			// Clear Memory
			memset(peer, 0, sizeof(SceNetAdhocctlPeerInfo));

			// Save Nickname
			peer->nickname = packet->name;

			// Save MAC Address
			peer->mac_addr = packet->mac;

			// Save IP Address
			peer->ip_addr = packet->ip;

			// Calculate final IP-specific Port Offset
			peer->port_offset = ((isOriPort && !isPrivateIP(peer->ip_addr)) ? 0 : portOffset);

			// TimeStamp
			peer->last_recv = CoreTiming::GetGlobalTimeUsScaled();

			// Link to existing Peers
			peer->next = friends;

			// Link into Peerlist
			friends = peer;
		}
	}
}

SceNetAdhocctlPeerInfo * findFriend(SceNetEtherAddr * MAC) {
	if (MAC == NULL) return NULL;

	// Friends Reference
	SceNetAdhocctlPeerInfo * peer = friends;

	// Iterate Friends
	for (; peer != NULL; peer = peer->next) {
		if (isMacMatch(&peer->mac_addr, MAC)) break;
	}

	// Return found friend
	return peer;
}

SceNetAdhocctlPeerInfo* findFriendByIP(uint32_t ip) {
	// Friends Reference
	SceNetAdhocctlPeerInfo* peer = friends;

	// Iterate Friends
	for (; peer != NULL; peer = peer->next) {
		if (peer->ip_addr == ip) break;
	}

	// Return found friend
	return peer;
}

// fd is a host socket
int IsSocketReady(int fd, bool readfd, bool writefd, int* errorcode, int timeoutUS) {
	fd_set readfds, writefds;
	timeval tval;

	// Avoid getting Fatal signal 6 (SIGABRT) on linux/android
	if (fd < 0) {
		if (errorcode != nullptr)
			*errorcode = EBADF;
		return SOCKET_ERROR;
	}
#if !defined(_WIN32)
	if (fd >= FD_SETSIZE) {
		if (errorcode != nullptr)
			*errorcode = EBADF;
		return SOCKET_ERROR;
	}
#endif

	FD_ZERO(&readfds);
	writefds = readfds;
	if (readfd) {	
		FD_SET(fd, &readfds);
	}
	if (writefd) {	
		FD_SET(fd, &writefds);
	}
	tval.tv_sec = timeoutUS / 1000000;
	tval.tv_usec = timeoutUS % 1000000;

	// Note: select will flags an unconnected TCP socket (ie. a freshly created socket without connecting first, or when connect failed with ECONNREFUSED on linux) as writeable/readable, thus can't be used to tell whether the connection has established or not.
	int ret = select(fd + 1, readfd? &readfds: nullptr, writefd? &writefds: nullptr, nullptr, &tval);
	if (errorcode != nullptr)
		*errorcode = (ret < 0 ? socket_errno : 0);

	return ret;
}

void changeBlockingMode(int fd, int nonblocking) {
	unsigned long on = 1;
	unsigned long off = 0;
#if defined(_WIN32)
	if (nonblocking) {
		// Change to Non-Blocking Mode
		ioctlsocket(fd, FIONBIO, &on);
	}
	else {
		// Change to Blocking Mode
		ioctlsocket(fd, FIONBIO, &off);
	}
// If they have O_NONBLOCK, use the POSIX way to do it. On POSIX sockets Error code would be EINPROGRESS instead of EAGAIN
//#elif defined(O_NONBLOCK)
#else
	int flags = fcntl(fd, F_GETFL, 0);
	// Fixme: O_NONBLOCK is defined but broken on SunOS 4.1.x and AIX 3.2.5.
	if (flags == -1)
		flags = 0;
	if (nonblocking) {
		// Set Non-Blocking Flag
		fcntl(fd, F_SETFL, flags | O_NONBLOCK);
	}
	else {
		// Remove Non-Blocking Flag
		fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
	}
// Otherwise, use the old way of doing it (UNIX way). On UNIX sockets Error code would be EAGAIN instead of EINPROGRESS
/*#else
	if (nonblocking) {
		// Change to Non - Blocking Mode
		ioctl(fd, FIONBIO, (char*)&on);
	}
	else {
		// Change to Blocking Mode
		ioctl(fd, FIONBIO, (char*)&off);
	}*/
#endif
}

int countAvailableNetworks(const bool excludeSelf) {
	// Network Count
	int count = 0;

	// Group Reference
	SceNetAdhocctlScanInfo * group = networks;

	// Count Groups
	for (; group != NULL && (!excludeSelf || !isLocalMAC(&group->bssid.mac_addr)); group = group->next) count++;

	// Return Network Count
	return count;
}

SceNetAdhocctlScanInfo * findGroup(SceNetEtherAddr * MAC) {
	if (MAC == NULL) return NULL;

	// Groups Reference
	SceNetAdhocctlScanInfo * group = networks;

	// Iterate Groups
	for (; group != NULL; group = group->next) {
		if (isMacMatch(&group->bssid.mac_addr, MAC)) break;
	}

	// Return found group
	return group;
}

void freeGroupsRecursive(SceNetAdhocctlScanInfo * node) {
	// End of List
	if (node == NULL) return;

	// Increase Recursion Depth
	freeGroupsRecursive(node->next);

	// Free Memory
	free(node);
	node = NULL;
}

void deleteAllAdhocSockets() {
	// Iterate Element
	for (int i = 0; i < MAX_SOCKET; i++) {
		// Active Socket
		if (adhocSockets[i] != NULL) {
			auto sock = adhocSockets[i];
			int fd = -1;

			if (sock->type == SOCK_PTP)
				fd = sock->data.ptp.id;
			else if (sock->type == SOCK_PDP)
				fd = sock->data.pdp.id;

			if (fd > 0) {
				// Close Socket
				struct linger sl {};
				sl.l_onoff = 1;		// non-zero value enables linger option in kernel
				sl.l_linger = 0;	// timeout interval in seconds
				setsockopt(fd, SOL_SOCKET, SO_LINGER, (const char*)&sl, sizeof(sl));
				shutdown(fd, SD_RECEIVE);
				closesocket(fd);
			}
			// Free Memory
			free(adhocSockets[i]);

			// Delete Reference
			adhocSockets[i] = NULL;
		}
	}
}

void deleteAllGMB() {
	if (gameModeBuffer) {
		free(gameModeBuffer);
		gameModeBuffer = nullptr;
		gameModeBuffSize = 0;
	}
	if (masterGameModeArea.data) {
		free(masterGameModeArea.data);
		masterGameModeArea = { 0 };
	}
	for (auto& it : replicaGameModeAreas) {
		if (it.data) {
			free(it.data);
			it.data = nullptr;
		}
	}
	replicaGameModeAreas.clear();
	gameModeMacs.clear();
	requiredGameModeMacs.clear();
}

void deleteFriendByIP(uint32_t ip) {
	// Previous Peer Reference
	SceNetAdhocctlPeerInfo * prev = NULL;

	// Peer Pointer
	SceNetAdhocctlPeerInfo * peer = friends;

	// Iterate Peers
	for (; peer != NULL; peer = peer->next) {
		// Found Peer
		if (peer->ip_addr == ip) {
			
			// Multithreading Lock
			peerlock.lock();

			// Unlink Left (Beginning)
			/*if (prev == NULL) friends = peer->next;

			// Unlink Left (Other)
			else prev->next = peer->next;
			*/

			u32 tmpip = peer->ip_addr;
			INFO_LOG(Log::sceNet, "Removing Friend Peer %s [%s]", mac2str(&peer->mac_addr).c_str(), ip2str(*(struct in_addr *)&tmpip).c_str()); //inet_ntoa(*(in_addr*)&peer->ip_addr)

			// Free Memory
			//free(peer);
			//peer = NULL;
			// Instead of removing it from the list we'll make it timed out since most Matching games are moving group and may still need the peer data thus not recognizing it as Unknown peer
			peer->last_recv = 0; //CoreTiming::GetGlobalTimeUsScaled();

			// Multithreading Unlock
			peerlock.unlock();

			// Stop Search
			break;
		}

		// Set Previous Reference
		prev = peer;
	}
}

int findFreeMatchingID() {
	// Minimum Matching ID
	int min = 1;

	// Maximum Matching ID
	int max = 0;

	// Find highest Matching ID
	SceNetAdhocMatchingContext * item = contexts; 
	for (; item != NULL; item = item->next) {
		// New Maximum
		if (max < item->id) max = item->id;
	}

	// Find unoccupied ID
	int i = min; 
	for (; i < max; i++) {
		// Found unoccupied ID
		if (findMatchingContext(i) == NULL) return i;
	}

	// Append at virtual end
	return max + 1;
}

SceNetAdhocMatchingContext * findMatchingContext(int id) {
	// Iterate Matching Context List
	SceNetAdhocMatchingContext * item = contexts; 
	for (; item != NULL; item = item->next) { // Found Matching ID
		if (item->id == id) return item;
	}

	// Context not found
	return NULL;
}

/**
* Find Outgoing Request Target Peer
* @param context Matching Context Pointer
* @return Internal Peer Reference or... NULL
*/
SceNetAdhocMatchingMemberInternal * findOutgoingRequest(SceNetAdhocMatchingContext * context)
{
	// Iterate Peer List for Matching Target
	SceNetAdhocMatchingMemberInternal * peer = context->peerlist; 
	for (; peer != NULL; peer = peer->next)
	{
		// Found Peer in List
		if (peer->state == PSP_ADHOC_MATCHING_PEER_OUTGOING_REQUEST) return peer;
	}

	// Peer not found
	return NULL;
}

/**
* Remove unneeded Peer Data after being accepted to a match
* @param context Matching Context Pointer
*/
void postAcceptCleanPeerList(SceNetAdhocMatchingContext * context)
{
	int delcount = 0;
	int peercount = 0;
	// Acquire Peer Lock
	peerlock.lock();

	// Iterate Peer List
	SceNetAdhocMatchingMemberInternal * peer = context->peerlist; 
	while (peer != NULL)
	{
		// Save next Peer just in case we have to delete this one
		SceNetAdhocMatchingMemberInternal * next = peer->next;

		// Unneeded Peer
		if (peer->state != PSP_ADHOC_MATCHING_PEER_CHILD && peer->state != PSP_ADHOC_MATCHING_PEER_P2P && peer->state != PSP_ADHOC_MATCHING_PEER_PARENT && peer->state != 0) {
			deletePeer(context, peer);
			delcount++;
		}

		// Move to Next Peer
		peer = next;
		peercount++;
	}

	// Free Peer Lock
	peerlock.unlock();

	INFO_LOG(Log::sceNet, "Removing Unneeded Peers (%i/%i)", delcount, peercount);
}

/**
* Add Sibling-Data that was sent with Accept-Datagram
* @param context Matching Context Pointer
* @param siblingcount Number of Siblings
* @param siblings Sibling MAC Array
*/
void postAcceptAddSiblings(SceNetAdhocMatchingContext * context, int siblingcount, SceNetEtherAddr * siblings)
{
	// Cast Sibling MAC Array to uint8_t
	// PSP CPU has a problem with non-4-byte aligned Pointer Access.
	// As the buffer of "siblings" isn't properly aligned I don't want to risk a crash.
	uint8_t * siblings_u8 = (uint8_t *)siblings;

	peerlock.lock();
	// Iterate Siblings. Reversed so these siblings are added into peerlist in the same order with the peerlist on host/parent side
	for (int i = siblingcount - 1; i >= 0 ; i--)
	{
		SceNetEtherAddr* mac = (SceNetEtherAddr*)(siblings_u8 + sizeof(SceNetEtherAddr) * i);

		auto peer = findPeer(context, mac);
		// Already exist
		if (peer != NULL) {
			// Set Peer State
			peer->state = PSP_ADHOC_MATCHING_PEER_CHILD;
			peer->sending = 0;
			peer->lastping = CoreTiming::GetGlobalTimeUsScaled();
			WARN_LOG(Log::sceNet, "Updating Sibling Peer %s", mac2str(mac).c_str());
		}
		else {
			// Allocate Memory
			SceNetAdhocMatchingMemberInternal* sibling = (SceNetAdhocMatchingMemberInternal*)malloc(sizeof(SceNetAdhocMatchingMemberInternal));

			// Allocated Memory
			if (sibling != NULL)
			{
				// Clear Memory
				memset(sibling, 0, sizeof(SceNetAdhocMatchingMemberInternal));

				// Save MAC Address
				memcpy(&sibling->mac, mac, sizeof(SceNetEtherAddr));

				// Set Peer State
				sibling->state = PSP_ADHOC_MATCHING_PEER_CHILD;

				// Initialize Ping Timer
				sibling->lastping = CoreTiming::GetGlobalTimeUsScaled(); //time_now_d()*1000000.0;

				// Link Peer
				sibling->next = context->peerlist;
				context->peerlist = sibling;

				// Spawn Established Event. FIXME: ESTABLISHED event should only be triggered for Parent/P2P peer?
				//spawnLocalEvent(context, PSP_ADHOC_MATCHING_EVENT_ESTABLISHED, &sibling->mac, 0, NULL);

				INFO_LOG(Log::sceNet, "Accepting Sibling Peer %s", mac2str(&sibling->mac).c_str());
			}
		}
	}
	peerlock.unlock();
}

/**
* Count Children Peers (for Parent)
* @param context Matching Context Pointer
* @return Number of Children
*/
s32_le countChildren(SceNetAdhocMatchingContext * context, const bool excludeTimedout)
{
	// Children Counter
	s32_le count = 0;

	// Iterate Peer List for Matching Target
	SceNetAdhocMatchingMemberInternal * peer = context->peerlist; 
	for (; peer != NULL; peer = peer->next)
	{
		// Exclude timedout members?
		if (!excludeTimedout || peer->lastping != 0)
		// Increase Children Counter
		if (peer->state == PSP_ADHOC_MATCHING_PEER_CHILD) count++;
	}

	// Return Children Count
	return count;
}

/**
* Find Peer in Context by MAC
* @param context Matching Context Pointer
* @param mac Peer MAC Address
* @return Internal Peer Reference or... NULL
*/
SceNetAdhocMatchingMemberInternal * findPeer(SceNetAdhocMatchingContext * context, SceNetEtherAddr * mac)
{
	if (mac == NULL)
		return NULL;

	// Iterate Peer List for Matching Target
	SceNetAdhocMatchingMemberInternal * peer = context->peerlist; 
	for (; peer != NULL; peer = peer->next)
	{
		// Found Peer in List
		if (isMacMatch(&peer->mac, mac))
		{
			// Return Peer Pointer
			return peer;
		}
	}

	// Peer not found
	return NULL;
}

/**
* Find Parent Peer
* @param context Matching Context Pointer
* @return Internal Peer Reference or... NULL
*/
SceNetAdhocMatchingMemberInternal * findParent(SceNetAdhocMatchingContext * context)
{
	// Iterate Peer List for Matching Target
	SceNetAdhocMatchingMemberInternal * peer = context->peerlist; 
	for (; peer != NULL; peer = peer->next)
	{
		// Found Peer in List
		if (peer->state == PSP_ADHOC_MATCHING_PEER_PARENT) return peer;
	}

	// Peer not found
	return NULL;
}

/**
* Find P2P Buddy Peer
* @param context Matching Context Pointer
* @return Internal Peer Reference or... NULL
*/
SceNetAdhocMatchingMemberInternal * findP2P(SceNetAdhocMatchingContext * context, const bool excludeTimedout)
{
	// Iterate Peer List for Matching Target
	SceNetAdhocMatchingMemberInternal * peer = context->peerlist; 
	for (; peer != NULL; peer = peer->next)
	{
		// Exclude timedout members?
		if (!excludeTimedout || peer->lastping != 0)
		// Found Peer in List
		if (peer->state == PSP_ADHOC_MATCHING_PEER_P2P) return peer;
	}

	// Peer not found
	return NULL;
}

/**
* Delete Peer from List
* @param context Matching Context Pointer
* @param peer Internal Peer Reference
*/
void deletePeer(SceNetAdhocMatchingContext * context, SceNetAdhocMatchingMemberInternal *& peer)
{
	// Valid Arguments
	if (context != NULL && peer != NULL)
	{
		peerlock.lock();

		// Previous Peer Reference
		SceNetAdhocMatchingMemberInternal * previous = NULL;

		// Iterate Peer List
		SceNetAdhocMatchingMemberInternal * item = context->peerlist; 
		for (; item != NULL; item = item->next)
		{
			// Found Peer Match
			if (item == peer) break;

			// Set Previous Peer
			previous = item;
		}

		if (item != NULL) {
			// Middle Item
			if (previous != NULL) previous->next = item->next;

			// Beginning Item
			else context->peerlist = item->next;

			INFO_LOG(Log::sceNet, "Removing Member Peer %s", mac2str(&peer->mac).c_str());
		}

		// Free Peer Memory
		free(peer);
		peer = NULL;

		peerlock.unlock();
	}
}

/**
* Safely Link Thread Message to Event Thread Stack
* @param context Matching Context Pointer
* @param message Thread Message Pointer
*/
void linkEVMessage(SceNetAdhocMatchingContext * context, ThreadMessage * message)
{
	// Lock Access
	context->eventlock->lock();

	// Link Message
	message->next = context->event_stack;
	context->event_stack = message;

	// Unlock Access
	context->eventlock->unlock();
}

/**
* Safely Link Thread Message to IO Thread Stack
* @param context Matching Context Pointer
* @param message Thread Message Pointer
*/
void linkIOMessage(SceNetAdhocMatchingContext * context, ThreadMessage * message)
{
	// Lock Access
	context->inputlock->lock();

	// Link Message
	message->next = context->input_stack;
	context->input_stack = message;

	// Unlock Access
	context->inputlock->unlock();
}

/**
* Send Generic Thread Message
* @param context Matching Context Pointer
* @param stack ADHOC_MATCHING_EVENT_STACK or ADHOC_MATCHING_INPUT_STACK
* @param mac Target MAC
* @param opcode Message Opcode
* @param optlen Optional Data Length
* @param opt Optional Data
*/
void sendGenericMessage(SceNetAdhocMatchingContext * context, int stack, SceNetEtherAddr * mac, int opcode, int optlen, const void * opt)
{
	// Calculate Required Memory Size
	uint32_t size = sizeof(ThreadMessage) + optlen;

	// Allocate Memory
	uint8_t * memory = (uint8_t *)malloc(size);

	// Allocated Memory
	if (memory != NULL)
	{
		// Clear Memory
		memset(memory, 0, size);

		// Cast Header
		ThreadMessage * header = (ThreadMessage *)memory;

		// Set Message Opcode
		header->opcode = opcode;

		// Set Peer MAC Address
		header->mac = *mac;

		// Set Optional Data Length
		header->optlen = optlen;

		// Set Optional Data
		memcpy(memory + sizeof(ThreadMessage), opt, optlen);

		// Link Thread Message
		if (stack == PSP_ADHOC_MATCHING_EVENT_STACK) linkEVMessage(context, header);

		// Link Thread Message to Input Stack
		else linkIOMessage(context, header);

		// Exit Function
		return;
	}

	peerlock.lock();
	// Out of Memory Emergency Delete
	auto peer = findPeer(context, mac);
	deletePeer(context, peer);
	peerlock.unlock();
}

/**
* Send Accept Message from P2P -> P2P or Parent -> Children
* @param context Matching Context Pointer
* @param peer Target Peer
* @param optlen Optional Data Length
* @param opt Optional Data
*/
void sendAcceptMessage(SceNetAdhocMatchingContext * context, SceNetAdhocMatchingMemberInternal * peer, int optlen, const void * opt)
{
	// Send Accept Message
	sendGenericMessage(context, PSP_ADHOC_MATCHING_INPUT_STACK, &peer->mac, PSP_ADHOC_MATCHING_PACKET_ACCEPT, optlen, opt);
}

/**
* Send Join Request from P2P -> P2P or Children -> Parent
* @param context Matching Context Pointer
* @param peer Target Peer
* @param optlen Optional Data Length
* @param opt Optional Data
*/
void sendJoinRequest(SceNetAdhocMatchingContext * context, SceNetAdhocMatchingMemberInternal * peer, int optlen, const void * opt)
{
	// Send Join Message
	sendGenericMessage(context, PSP_ADHOC_MATCHING_INPUT_STACK, &peer->mac, PSP_ADHOC_MATCHING_PACKET_JOIN, optlen, opt);
}

/**
* Send Cancel Message to Peer (has various effects)
* @param context Matching Context Pointer
* @param peer Target Peer
* @param optlen Optional Data Length
* @param opt Optional Data
*/
void sendCancelMessage(SceNetAdhocMatchingContext * context, SceNetAdhocMatchingMemberInternal * peer, int optlen, const void * opt)
{
	// Send Cancel Message
	sendGenericMessage(context, PSP_ADHOC_MATCHING_INPUT_STACK, &peer->mac, PSP_ADHOC_MATCHING_PACKET_CANCEL, optlen, opt);
}

/**
* Send Bulk Data to Peer
* @param context Matching Context Pointer
* @param peer Target Peer
* @param datalen Data Length
* @param data Data
*/
void sendBulkData(SceNetAdhocMatchingContext * context, SceNetAdhocMatchingMemberInternal * peer, int datalen, const void * data)
{
	// Send Bulk Data Message
	sendGenericMessage(context, PSP_ADHOC_MATCHING_INPUT_STACK, &peer->mac, PSP_ADHOC_MATCHING_PACKET_BULK, datalen, data);
}

/**
* Abort Bulk Data Transfer (if in progress)
* @param context Matching Context Pointer
* @param peer Target Peer
*/
void abortBulkTransfer(SceNetAdhocMatchingContext * context, SceNetAdhocMatchingMemberInternal * peer)
{
	// Send Bulk Data Abort Message
	sendGenericMessage(context, PSP_ADHOC_MATCHING_INPUT_STACK, &peer->mac, PSP_ADHOC_MATCHING_PACKET_BULK_ABORT, 0, NULL);
}

/**
* Notify all established Peers about new Kid in the Neighborhood
* @param context Matching Context Pointer
* @param peer New Kid
*/
void sendBirthMessage(SceNetAdhocMatchingContext * context, SceNetAdhocMatchingMemberInternal * peer)
{
	// Send Birth Message
	sendGenericMessage(context, PSP_ADHOC_MATCHING_INPUT_STACK, &peer->mac, PSP_ADHOC_MATCHING_PACKET_BIRTH, 0, NULL);
}

/**
* Notify all established Peers about abandoned Child
* @param context Matching Context Pointer
* @param peer Abandoned Child
*/
void sendDeathMessage(SceNetAdhocMatchingContext * context, SceNetAdhocMatchingMemberInternal * peer)
{
	// Send Death Message
	sendGenericMessage(context, PSP_ADHOC_MATCHING_INPUT_STACK, &peer->mac, PSP_ADHOC_MATCHING_PACKET_DEATH, 0, NULL);
}

/**
* Return Number of Connected Peers
* @param context Matching Context Pointer
* @return Number of Connected Peers
*/
uint32_t countConnectedPeers(SceNetAdhocMatchingContext * context, const bool excludeTimedout)
{
	// Peer Count
	uint32_t count = 0;

	// Parent Mode
	if (context->mode == PSP_ADHOC_MATCHING_MODE_PARENT)
	{
		// Number of Children + 1 Parent (Self)
		count = countChildren(context, excludeTimedout) + 1;
	}

	// Child Mode
	else if (context->mode == PSP_ADHOC_MATCHING_MODE_CHILD)
	{
		// Default to 1 Child (Self)
		count = 1;

		// Connected to Parent
		if (findParent(context) != NULL)
		{
			// Add Number of Siblings + 1 Parent
			count += countChildren(context, excludeTimedout) + 1; // Since count is already started from 1, Do we need to +1 here? Ys vs. Sora no Kiseki seems to show wrong number of players without +1 here
		}
	}

	// P2P Mode
	else
	{
		// Default to 1 P2P Client (Self)
		count = 1;

		// Connected to another P2P Client
		if (findP2P(context, excludeTimedout) != NULL)
		{
			// Add P2P Brother
			count++;
		}
	}

	// Return Peer Count
	return count;
}

/**
* Spawn Local Event for Event Thread
* @param context Matching Context Pointer
* @param event Event ID
* @param mac Event Source MAC
* @param optlen Optional Data Length
* @param opt Optional Data
*/
void spawnLocalEvent(SceNetAdhocMatchingContext * context, int event, SceNetEtherAddr * mac, int optlen, void * opt)
{
	// Spawn Local Event
	sendGenericMessage(context, PSP_ADHOC_MATCHING_EVENT_STACK, mac, event, optlen, opt);
}

/**
* Handle Timeouts in Matching Context
* @param context Matching Context Pointer
*/
void handleTimeout(SceNetAdhocMatchingContext * context)
{
	peerlock.lock();
	// Iterate Peer List
	SceNetAdhocMatchingMemberInternal * peer = context->peerlist; 
	while (peer != NULL && contexts != NULL && coreState != CORE_POWERDOWN)
	{
		// Get Next Pointer (to avoid crash on memory freeing)
		SceNetAdhocMatchingMemberInternal * next = peer->next;

		u64_le now = CoreTiming::GetGlobalTimeUsScaled(); //time_now_d()*1000000.0
		// Timeout! Apparently the latest GetGlobalTimeUsScaled (ie. now) have a possibility to be smaller than previous GetGlobalTimeUsScaled (ie. lastping) thus resulting a negative number when subtracted :(
		if (peer->state != 0 && static_cast<s64>(now - peer->lastping) > static_cast<s64>(context->timeout)) 
		{
			// Spawn Timeout Event. FIXME: Should we allow TIMEOUT Event to intervene joining process of Parent-Child too just like P2P Mode? (ie. Crazy Taxi uses P2P Mode)
			if ((context->mode == PSP_ADHOC_MATCHING_MODE_CHILD && peer->state == PSP_ADHOC_MATCHING_PEER_PARENT) ||
				(context->mode == PSP_ADHOC_MATCHING_MODE_PARENT && peer->state == PSP_ADHOC_MATCHING_PEER_CHILD) ||
				(context->mode == PSP_ADHOC_MATCHING_MODE_P2P && 
					(peer->state == PSP_ADHOC_MATCHING_PEER_P2P || peer->state == PSP_ADHOC_MATCHING_PEER_OFFER || peer->state == PSP_ADHOC_MATCHING_PEER_INCOMING_REQUEST || peer->state == PSP_ADHOC_MATCHING_PEER_OUTGOING_REQUEST || peer->state == PSP_ADHOC_MATCHING_PEER_CANCEL_IN_PROGRESS)))
			{
				// FIXME: TIMEOUT event should only be triggered on Parent/P2P mode and for Parent/P2P peer?
				spawnLocalEvent(context, PSP_ADHOC_MATCHING_EVENT_TIMEOUT, &peer->mac, 0, NULL);

				INFO_LOG(Log::sceNet, "TimedOut Member Peer %s (%lld - %lld = %lld > %lld us)", mac2str(&peer->mac).c_str(), now, peer->lastping, (now - peer->lastping), context->timeout);

				if (context->mode == PSP_ADHOC_MATCHING_MODE_PARENT) 
					sendDeathMessage(context, peer);
				else 
					sendCancelMessage(context, peer, 0, NULL);
			}
		}

		// Move Pointer
		peer = next;
	}
	peerlock.unlock();
}

/**
* Recursive Stack Cleaner
* @param node Current Thread Message Node
*/
void clearStackRecursive(ThreadMessage *& node)
{
	// Not End of List
	if (node != NULL) clearStackRecursive(node->next);

	// Free Last Existing Node of List (NULL is handled in _free)
	free(node);
	node = NULL;
}

/**
* Clear Thread Stack
* @param context Matching Context Pointer
* @param stack ADHOC_MATCHING_EVENT_STACK or ADHOC_MATCHING_INPUT_STACK
*/
void clearStack(SceNetAdhocMatchingContext * context, int stack)
{
	if (context == NULL) return;

	// Clear Event Stack
	if (stack == PSP_ADHOC_MATCHING_EVENT_STACK)
	{
		context->eventlock->lock();
		// Free Memory Recursively
		clearStackRecursive(context->event_stack);

		// Destroy Reference
		context->event_stack = NULL;
		
		context->eventlock->unlock();
	}

	// Clear IO Stack
	else
	{
		context->inputlock->lock();
		// Free Memory Recursively
		clearStackRecursive(context->input_stack);

		// Destroy Reference
		context->input_stack = NULL;

		context->inputlock->unlock();
	}
}

/**
* Clear Peer List
* @param context Matching Context Pointer
*/
void clearPeerList(SceNetAdhocMatchingContext * context)
{
	// Acquire Peer Lock
	peerlock.lock();

	// Iterate Peer List
	SceNetAdhocMatchingMemberInternal * peer = context->peerlist; 
	while (peer != NULL)
	{
		// Grab Next Pointer
		context->peerlist = peer->next; //SceNetAdhocMatchingMemberInternal * next = peer->next;

		// Delete Peer
		free(peer); //deletePeer(context, peer);
		// Instead of removing peer immediately, We should give a little time before removing the peer and let it timed out? just in case the game is in the middle of communicating with the peer on another thread so it won't recognize it as Unknown peer
		//peer->lastping = CoreTiming::GetGlobalTimeUsScaled();

		// Move Pointer
		peer = context->peerlist; //peer = next;
	}

	// Free Peer Lock
	peerlock.unlock();
}

void AfterMatchingMipsCall::DoState(PointerWrap & p) {
	auto s = p.Section("AfterMatchingMipsCall", 1, 4);
	if (!s)
		return;
	if (s >= 1) {
		Do(p, EventID);
	} else {
		EventID = -1;
	}
	if (s >= 4) {
		Do(p, contextID);
		Do(p, bufAddr);
	} else {
		contextID = -1;
		bufAddr = 0;
	}
}

// It seems After Actions being called in reverse order of Mipscall order (ie. MipsCall order of ACCEPT(6)->ESTABLISH(7) getting AfterAction order of ESTABLISH(7)->ACCEPT(6)
void AfterMatchingMipsCall::run(MipsCall &call) {
	if (context == NULL) {
		peerlock.lock();
		context = findMatchingContext(contextID);
		peerlock.unlock();
	}
	u32 v0 = currentMIPS->r[MIPS_REG_V0];
	if (__IsInInterrupt()) ERROR_LOG(Log::sceNet, "AfterMatchingMipsCall::run [ID=%i][Event=%d] is Returning Inside an Interrupt!", contextID, EventID);
	//SetMatchingInCallback(context, false);
	DEBUG_LOG(Log::sceNet, "AfterMatchingMipsCall::run [ID=%i][Event=%d][%s] [cbId: %u][retV0: %08x]", contextID, EventID, mac2str((SceNetEtherAddr*)Memory::GetPointer(bufAddr)).c_str(), call.cbId, v0);
	if (Memory::IsValidAddress(bufAddr)) userMemory.Free(bufAddr);
	//call.setReturnValue(v0);
}

void AfterMatchingMipsCall::SetData(int ContextID, int eventId, u32_le BufAddr) {
	contextID = ContextID;
	EventID = eventId;
	bufAddr = BufAddr;
	peerlock.lock();
	context = findMatchingContext(ContextID);
	peerlock.unlock();
}

bool SetMatchingInCallback(SceNetAdhocMatchingContext* context, bool IsInCB) {
	if (context == NULL) return false;
	peerlock.lock();
	context->IsMatchingInCB = IsInCB;
	peerlock.unlock();
	return IsInCB;
}

bool IsMatchingInCallback(SceNetAdhocMatchingContext* context) {
	bool inCB = false;
	if (context == NULL) return inCB;
	peerlock.lock();
	inCB = (context->IsMatchingInCB);
	peerlock.unlock();
	return inCB;
}

void AfterAdhocMipsCall::DoState(PointerWrap & p) {
	auto s = p.Section("AfterAdhocMipsCall", 1, 4);
	if (!s)
		return;
	if (s >= 3) {
		Do(p, HandlerID);
		Do(p, EventID);
		Do(p, argsAddr);
	} else {
		HandlerID = -1;
		EventID = -1;
		argsAddr = 0;
	}
}

void AfterAdhocMipsCall::run(MipsCall& call) {
	u32 v0 = currentMIPS->r[MIPS_REG_V0];
	if (__IsInInterrupt()) ERROR_LOG(Log::sceNet, "AfterAdhocMipsCall::run [ID=%i][Event=%d] is Returning Inside an Interrupt!", HandlerID, EventID);
	SetAdhocctlInCallback(false);
	isAdhocctlBusy = false;
	DEBUG_LOG(Log::sceNet, "AfterAdhocMipsCall::run [ID=%i][Event=%d] [cbId: %u][retV0: %08x]", HandlerID, EventID, call.cbId, v0);
	//call.setReturnValue(v0);
}

void AfterAdhocMipsCall::SetData(int handlerID, int eventId, u32_le ArgsAddr) {
	HandlerID = handlerID;
	EventID = eventId;
	argsAddr = ArgsAddr;
}

int SetAdhocctlInCallback(bool IsInCB) {
	std::lock_guard<std::recursive_mutex> adhocGuard(adhocEvtMtx);
	IsAdhocctlInCB += (IsInCB?1:-1);
	return IsAdhocctlInCB;
}

int IsAdhocctlInCallback() {
	std::lock_guard<std::recursive_mutex> adhocGuard(adhocEvtMtx);
	int inCB = IsAdhocctlInCB;
	return inCB;
}

// Make sure MIPS calls have been fully executed before the next notifyAdhocctlHandlers
void notifyAdhocctlHandlers(u32 flag, u32 error) {
	__UpdateAdhocctlHandlers(flag, error);
}

void freeFriendsRecursive(SceNetAdhocctlPeerInfo * node, int32_t* count) {
	// End of List
	if (node == NULL) return;

	// Increase Recursion Depth
	freeFriendsRecursive(node->next, count);

	// Free Memory
	free(node);
	node = NULL;
	if (count != NULL) (*count)++;
}

void timeoutFriendsRecursive(SceNetAdhocctlPeerInfo * node, int32_t* count) {
	// End of List
	if (node == NULL) return;

	// Increase Recursion Depth
	timeoutFriendsRecursive(node->next, count);

	// Set last timestamp
	node->last_recv = 0;
	if (count != NULL) (*count)++;
}

void sendChat(const std::string &chatString) {
	SceNetAdhocctlChatPacketC2S chat{};
	chat.base.opcode = OPCODE_CHAT;
	//TODO check network inited, check send success or not, chatlog.pushback error on failed send, pushback error on not connected
	if (friendFinderRunning) {
		// Send Chat to Server 
		if (!chatString.empty()) {
			//maximum char allowed is 64 character for compability with original server (pro.coldbird.net)
			std::string message = chatString.substr(0, 60); // 64 return chat variable corrupted is it out of memory?
			strcpy(chat.message, message.c_str());
			//Send Chat Messages
			if (IsSocketReady((int)metasocket, false, true) > 0) {
				int chatResult = (int)send((int)metasocket, (const char*)&chat, sizeof(chat), MSG_NOSIGNAL);
				NOTICE_LOG(Log::sceNet, "Send Chat %s to Adhoc Server", chat.message);
				std::string name = g_Config.sNickName;

				std::lock_guard<std::mutex> guard(chatLogLock);
				chatLog.emplace_back(name.substr(0, 8) + ": " + chat.message);
				chatMessageGeneration++;
			}
		}
	} else {
		std::lock_guard<std::mutex> guard(chatLogLock);
		auto n = GetI18NCategory(I18NCat::NETWORKING);
		chatLog.push_back(std::string(n->T("You're in Offline Mode, go to lobby or online hall")));
		chatMessageGeneration++;
	}
}

std::vector<std::string> getChatLog() {
	std::lock_guard<std::mutex> guard(chatLogLock);
	// If the log gets large, trim it down.
	if (chatLog.size() > 50) {
		chatLog.erase(chatLog.begin(), chatLog.begin() + (chatLog.size() - 50));
	}
	return chatLog;
}

int GetChatChangeID() {
	return chatMessageGeneration;
}

int GetChatMessageCount() {
	return chatMessageCount;
}

// TODO: We should probably change this thread into PSPThread (or merging it into the existing AdhocThread PSPThread) as there are too many global vars being used here which also being used within some HLEs
int friendFinder() {
	SetCurrentThreadName("FriendFinder");
	auto n = GetI18NCategory(I18NCat::NETWORKING);
	// Receive Buffer
	int rxpos = 0;
	uint8_t rx[1024];

	// Chat Packet
	SceNetAdhocctlChatPacketC2S chat;
	chat.base.opcode = OPCODE_CHAT;

	// Last Ping Time
	uint64_t lastping = 0;

	// Last Time Reception got updated
	uint64_t lastreceptionupdate = 0;

	uint64_t now;

	// Log Startup
	INFO_LOG(Log::sceNet, "FriendFinder: Begin of Friend Finder Thread");

	// Resolve and cache AdhocServer DNS
	addrinfo* resolved = nullptr;
	std::string err;
	g_adhocServerIP.in.sin_addr.s_addr = INADDR_NONE;
	if (g_Config.bEnableWlan && !net::DNSResolve(g_Config.proAdhocServer, "", &resolved, err)) {
		ERROR_LOG(Log::sceNet, "DNS Error Resolving %s\n", g_Config.proAdhocServer.c_str());
		g_OSD.Show(OSDType::MESSAGE_ERROR, std::string(n->T("DNS Error Resolving ")) + g_Config.proAdhocServer);
	}
	if (resolved) {
		for (auto ptr = resolved; ptr != NULL; ptr = ptr->ai_next) {
			switch (ptr->ai_family) {
			case AF_INET:
				g_adhocServerIP.in = *(sockaddr_in*)ptr->ai_addr;
				break;
			}
		}
		net::DNSResolveFree(resolved);
	}
	g_adhocServerIP.in.sin_port = htons(SERVER_PORT);

	// Finder Loop
	friendFinderRunning = true;
	while (friendFinderRunning) {
		// Acquire Network Lock
		//_acquireNetworkLock();

		// Reconnect when disconnected while Adhocctl is still inited
		if (metasocket == (int)INVALID_SOCKET && netAdhocctlInited && isAdhocctlNeedLogin) {
			if (g_Config.bEnableWlan) {
				// Not really initNetwork.
				if (initNetwork(&product_code) == 0) {
					g_adhocServerConnected = true;
					INFO_LOG(Log::sceNet, "FriendFinder: Network [RE]Initialized");
					// At this point we are most-likely not in a Group within the Adhoc Server, so we should probably reset AdhocctlState
					adhocctlState = ADHOCCTL_STATE_DISCONNECTED;
					netAdhocGameModeEntered = false;
					isAdhocctlBusy = false;
				} 
				else {
					g_adhocServerConnected = false;
					shutdown((int)metasocket, SD_BOTH);
					closesocket((int)metasocket);
					metasocket = (int)INVALID_SOCKET;
				}
			}
		}

		// Prevent retrying to Login again unless it was on demand
		isAdhocctlNeedLogin = false;

		if (g_adhocServerConnected) {
			// Ping Server
			now = time_now_d() * 1000000.0; // Use time_now_d()*1000000.0 instead of CoreTiming::GetGlobalTimeUsScaled() if the game gets disconnected from AdhocServer too soon when FPS wasn't stable
			// original code : ((sceKernelGetSystemTimeWide() - lastping) >= ADHOCCTL_PING_TIMEOUT)
			if (static_cast<s64>(now - lastping) >= PSP_ADHOCCTL_PING_TIMEOUT) { // We may need to use lower interval to prevent getting timeout at Pro Adhoc Server through internet
				// Prepare Packet
				uint8_t opcode = OPCODE_PING;

				// Send Ping to Server, may failed with socket error 10054/10053 if someone else with the same IP already connected to AdHoc Server (the server might need to be modified to differentiate MAC instead of IP)
				if (IsSocketReady((int)metasocket, false, true) > 0) {
					int iResult = (int)send((int)metasocket, (const char*)&opcode, 1, MSG_NOSIGNAL);
					int error = socket_errno;
					// KHBBS seems to be getting error 10053 often
					if (iResult == SOCKET_ERROR) {
						ERROR_LOG(Log::sceNet, "FriendFinder: Socket Error (%i) when sending OPCODE_PING", error);
						if (error != EAGAIN && error != EWOULDBLOCK) {
							g_adhocServerConnected = false;
							shutdown((int)metasocket, SD_BOTH);
							closesocket((int)metasocket);
							metasocket = (int)INVALID_SOCKET;
							g_OSD.Show(OSDType::MESSAGE_ERROR, std::string(n->T("Disconnected from AdhocServer")) + " (" + std::string(n->T("Error")) + ": " + std::to_string(error) + ")");
							// Mark all friends as timedout since we won't be able to detects disconnected friends anymore without being connected to Adhoc Server
							peerlock.lock();
							timeoutFriendsRecursive(friends);
							peerlock.unlock();
						}
					}
					else {
						// Update Ping Time
						lastping = now;
						VERBOSE_LOG(Log::sceNet, "FriendFinder: Sending OPCODE_PING (%llu)", static_cast<unsigned long long>(now));
					}
				}
			}

			// Check for Incoming Data
			if (IsSocketReady((int)metasocket, true, false) > 0) {
				int received = (int)recv((int)metasocket, (char*)(rx + rxpos), sizeof(rx) - rxpos, MSG_NOSIGNAL);

				// Free Network Lock
				//_freeNetworkLock();

				// Received Data
				if (received > 0) {
					// Fix Position
					rxpos += received;

					// Log Incoming Traffic
					//printf("Received %d Bytes of Data from Server\n", received);
					INFO_LOG(Log::sceNet, "Received %d Bytes of Data from Adhoc Server", received);
				}
			}

			// Calculate EnterGameMode Timeout to prevent waiting forever for disconnected players
			if (isAdhocctlBusy && adhocctlState == ADHOCCTL_STATE_DISCONNECTED && adhocctlCurrentMode == ADHOCCTL_MODE_GAMEMODE && netAdhocGameModeEntered && static_cast<s64>(now - adhocctlStartTime) > netAdhocEnterGameModeTimeout) {
				netAdhocGameModeEntered = false;
				notifyAdhocctlHandlers(ADHOCCTL_EVENT_ERROR, ERROR_NET_ADHOC_TIMEOUT);
			}

			// Handle Packets
			if (rxpos > 0) {
				// BSSID Packet
				if (rx[0] == OPCODE_CONNECT_BSSID) {
					// Enough Data available
					if (rxpos >= (int)sizeof(SceNetAdhocctlConnectBSSIDPacketS2C)) {
						// Cast Packet
						SceNetAdhocctlConnectBSSIDPacketS2C* packet = (SceNetAdhocctlConnectBSSIDPacketS2C*)rx;

						INFO_LOG(Log::sceNet, "FriendFinder: Incoming OPCODE_CONNECT_BSSID [%s]", mac2str(&packet->mac).c_str());
						// Update Group BSSID
						parameter.bssid.mac_addr = packet->mac; // This packet seems to contains Adhoc Group Creator's BSSID (similar to AP's BSSID) so it shouldn't get mixed up with local MAC address. Note: On JPCSP + prx files params.bssid is hardcoded to "Jpcsp\0" and doesn't match to any of player's mac

						// From JPCSP: Some games have problems when the PSP_ADHOCCTL_EVENT_CONNECTED is sent too quickly after connecting to a network. The connection will be set CONNECTED with a small delay (200ms or 200us?)
						// Notify Event Handlers
						if (adhocctlCurrentMode == ADHOCCTL_MODE_GAMEMODE) {
							SceNetEtherAddr localMac;
							getLocalMac(&localMac);
							if (std::find_if(gameModeMacs.begin(), gameModeMacs.end(),
								[localMac](SceNetEtherAddr const& e) {
									return isMacMatch(&e, &localMac);
								}) == gameModeMacs.end()) {
								// Arrange the order to be consistent on all players (Host on top), Starting from our self the rest of new players will be added to the back
								gameModeMacs.push_back(localMac);

								// FIXME: OPCODE_CONNECT_BSSID only triggered once, but the timing of ADHOCCTL_EVENT_GAME notification could be too soon, since there could be more players that need to join before the event should be notified
								if (netAdhocGameModeEntered && gameModeMacs.size() >= requiredGameModeMacs.size()) {
									notifyAdhocctlHandlers(ADHOCCTL_EVENT_GAME, 0);
								}
							}
							else
								WARN_LOG(Log::sceNet, "GameMode SelfMember [%s] Already Existed!", mac2str(&localMac).c_str());
						}
						else {
							//adhocctlState = ADHOCCTL_STATE_CONNECTED;
							notifyAdhocctlHandlers(ADHOCCTL_EVENT_CONNECT, 0);
						}

						// Give time a little time
						//sceKernelDelayThread(adhocEventDelayMS * 1000);
						//sleep_ms(adhocEventDelayMS);

						// Move RX Buffer
						memmove(rx, rx + sizeof(SceNetAdhocctlConnectBSSIDPacketS2C), sizeof(rx) - sizeof(SceNetAdhocctlConnectBSSIDPacketS2C));

						// Fix RX Buffer Length
						rxpos -= sizeof(SceNetAdhocctlConnectBSSIDPacketS2C);
					}
				}

				// Chat Packet
				else if (rx[0] == OPCODE_CHAT) {
					// Enough Data available
					if (rxpos >= (int)sizeof(SceNetAdhocctlChatPacketS2C)) {
						// Cast Packet
						SceNetAdhocctlChatPacketS2C* packet = (SceNetAdhocctlChatPacketS2C*)rx;
						INFO_LOG(Log::sceNet, "FriendFinder: Incoming OPCODE_CHAT");

						// Fix strings with null-terminated
						packet->name.data[ADHOCCTL_NICKNAME_LEN - 1] = 0;
						packet->base.message[ADHOCCTL_MESSAGE_LEN - 1] = 0;

						// Add Incoming Chat to HUD
						NOTICE_LOG(Log::sceNet, "Received chat message %s", packet->base.message);
						std::string incoming = "";
						std::string name = (char*)packet->name.data;
						incoming.append(name.substr(0, 8));
						incoming.append(": ");
						incoming.append((char*)packet->base.message);

						std::lock_guard<std::mutex> guard(chatLogLock);
						chatLog.push_back(incoming);
						chatMessageGeneration++;
						chatMessageCount++;

						// Move RX Buffer
						memmove(rx, rx + sizeof(SceNetAdhocctlChatPacketS2C), sizeof(rx) - sizeof(SceNetAdhocctlChatPacketS2C));

						// Fix RX Buffer Length
						rxpos -= sizeof(SceNetAdhocctlChatPacketS2C);
					}
				}

				// Connect Packet
				else if (rx[0] == OPCODE_CONNECT) {
					// Enough Data available
					if (rxpos >= (int)sizeof(SceNetAdhocctlConnectPacketS2C)) {
						// Cast Packet
						SceNetAdhocctlConnectPacketS2C* packet = (SceNetAdhocctlConnectPacketS2C*)rx;

						// Fix strings with null-terminated
						packet->name.data[ADHOCCTL_NICKNAME_LEN - 1] = 0;

						// Log Incoming Peer
                        u32_le ipaddr = packet->ip;
						INFO_LOG(Log::sceNet, "FriendFinder: Incoming OPCODE_CONNECT [%s][%s][%s]", mac2str(&packet->mac).c_str(), ip2str(*(in_addr*)&ipaddr).c_str(), packet->name.data);

						// Add User
						addFriend(packet);

						// Make sure GameMode participants are all joined (including self MAC)
						if (adhocctlCurrentMode == ADHOCCTL_MODE_GAMEMODE) {
							if (std::find_if(gameModeMacs.begin(), gameModeMacs.end(),
								[packet](SceNetEtherAddr const& e) {
									return isMacMatch(&e, &packet->mac);
								}) == gameModeMacs.end()) {
								// Arrange the order to be consistent on all players (Host on top), Existing players are sent in reverse by AdhocServer
								SceNetEtherAddr localMac;
								getLocalMac(&localMac);
								auto it = std::find_if(gameModeMacs.begin(), gameModeMacs.end(),
									[localMac](SceNetEtherAddr const& e) {
										return isMacMatch(&e, &localMac);
									});
								// Starting from our self the rest of new players will be added to the back
								if (it != gameModeMacs.end()) {
									gameModeMacs.push_back(packet->mac);
								}
								else {
									it = gameModeMacs.begin() + 1;
									gameModeMacs.insert(it, packet->mac);
								}

								// From JPCSP: Join complete when all the required MACs have joined
								if (netAdhocGameModeEntered && requiredGameModeMacs.size() > 0 && gameModeMacs.size() == requiredGameModeMacs.size()) {
									// TODO: Should we replace gameModeMacs contents with requiredGameModeMacs contents to make sure they are in the same order with macs from sceNetAdhocctlCreateEnterGameMode? But may not be consistent with the list on client side!
									//gameModeMacs = requiredGameModeMacs;
									notifyAdhocctlHandlers(ADHOCCTL_EVENT_GAME, 0);
								}
							}
							else
								WARN_LOG(Log::sceNet, "GameMode Member [%s] Already Existed!", mac2str(&packet->mac).c_str());
						}

						// Update HUD User Count
						std::string name = (char*)packet->name.data;
						std::string incoming = "";
						incoming.append(name.substr(0, 8));
						incoming.append(" Joined ");
						//do we need ip?
						//joined.append((char *)packet->ip);

						std::lock_guard<std::mutex> guard(chatLogLock);
						chatLog.push_back(incoming);
						chatMessageGeneration++;

#ifdef LOCALHOST_AS_PEER
						setUserCount(getActivePeerCount());
#else
						// setUserCount(getActivePeerCount()+1);
#endif

						// Move RX Buffer
						memmove(rx, rx + sizeof(SceNetAdhocctlConnectPacketS2C), sizeof(rx) - sizeof(SceNetAdhocctlConnectPacketS2C));

						// Fix RX Buffer Length
						rxpos -= sizeof(SceNetAdhocctlConnectPacketS2C);
					}
				}

				// Disconnect Packet
				else if (rx[0] == OPCODE_DISCONNECT) {
					// Enough Data available
					if (rxpos >= (int)sizeof(SceNetAdhocctlDisconnectPacketS2C)) {
						// Cast Packet
						SceNetAdhocctlDisconnectPacketS2C* packet = (SceNetAdhocctlDisconnectPacketS2C*)rx;

						DEBUG_LOG(Log::sceNet, "FriendFinder: OPCODE_DISCONNECT");

						// Log Incoming Peer Delete Request
						INFO_LOG(Log::sceNet, "FriendFinder: Incoming Peer Data Delete Request...");

						if (adhocctlCurrentMode == ADHOCCTL_MODE_GAMEMODE) {
							auto peer = findFriendByIP(packet->ip);
							for (auto& gma : replicaGameModeAreas)
								if (isMacMatch(&gma.mac, &peer->mac_addr)) {
									gma.updateTimestamp = 0;
									break;
								}
						}

						// Delete User by IP, should delete by MAC since IP can be shared (behind NAT) isn't?
						deleteFriendByIP(packet->ip);

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
				else if (rx[0] == OPCODE_SCAN) {
					// Enough Data available
					if (rxpos >= (int)sizeof(SceNetAdhocctlScanPacketS2C)) {
						// Cast Packet
						SceNetAdhocctlScanPacketS2C* packet = (SceNetAdhocctlScanPacketS2C*)rx;

						DEBUG_LOG(Log::sceNet, "FriendFinder: OPCODE_SCAN");

						// Log Incoming Network Information
						INFO_LOG(Log::sceNet, "Incoming Group Information...");

						// Multithreading Lock
						peerlock.lock();

						// Allocate Structure Data
						SceNetAdhocctlScanInfo* group = (SceNetAdhocctlScanInfo*)malloc(sizeof(SceNetAdhocctlScanInfo));

						// Allocated Structure Data
						if (group != NULL) {
							// Clear Memory, should this be done only when allocating new group?
							memset(group, 0, sizeof(SceNetAdhocctlScanInfo));

							// Link to existing Groups
							group->next = newnetworks;

							// Copy Group Name
							group->group_name = packet->group;

							// Set Group Host
							group->bssid.mac_addr = packet->mac;

							// Set group parameters
							// Since 0 is not a valid active channel we fake the channel for Automatic Channel (JPCSP use 11 as default). Ridge Racer 2 will ignore any groups with channel 0 or that doesn't matched with channel value returned from sceUtilityGetSystemParamInt (which mean sceUtilityGetSystemParamInt must not return channel 0 when connected to a network?)
							group->channel = parameter.channel; //(parameter.channel == PSP_SYSTEMPARAM_ADHOC_CHANNEL_AUTOMATIC) ? defaultWlanChannel : parameter.channel;
							// This Mode should be a valid mode (>=0), probably should be sent by AdhocServer since there are 2 possibilities (Normal and GameMode). Air Conflicts - Aces Of World War 2 (which use GameMode) seems to relies on this Mode value.
							group->mode = std::max(ADHOCCTL_MODE_NORMAL, adhocctlCurrentMode); // default to ADHOCCTL_MODE_NORMAL

							// Link into Group List
							newnetworks = group;
						}

						// Multithreading Unlock
						peerlock.unlock();

						// Move RX Buffer
						memmove(rx, rx + sizeof(SceNetAdhocctlScanPacketS2C), sizeof(rx) - sizeof(SceNetAdhocctlScanPacketS2C));

						// Fix RX Buffer Length
						rxpos -= sizeof(SceNetAdhocctlScanPacketS2C);
					}
				}

				// Scan Complete Packet
				else if (rx[0] == OPCODE_SCAN_COMPLETE) {
					DEBUG_LOG(Log::sceNet, "FriendFinder: OPCODE_SCAN_COMPLETE");
					// Log Scan Completion
					INFO_LOG(Log::sceNet, "FriendFinder: Incoming Scan complete response...");

					// Reset current networks to prevent disbanded host to be listed again
					peerlock.lock();
					if (networks != newnetworks) {
						freeGroupsRecursive(networks);
						networks = newnetworks;
					}
					newnetworks = NULL;
					peerlock.unlock();

					// Notify Event Handlers
					notifyAdhocctlHandlers(ADHOCCTL_EVENT_SCAN, 0);

					// Move RX Buffer
					memmove(rx, rx + 1, sizeof(rx) - 1);

					// Fix RX Buffer Length
					rxpos -= 1;
				}
			}
		}
		// This delay time should be 100ms when there is an event otherwise 500ms ?
		sleep_ms(10, "pro-adhoc-poll-2"); // Using 1ms for faster response just like AdhocServer?

		// Don't do anything if it's paused, otherwise the log will be flooded
		while (Core_IsStepping() && coreState != CORE_POWERDOWN && friendFinderRunning)
			sleep_ms(10, "pro-adhoc-paused-poll-2");
	}

	// Groups/Networks should be deallocated isn't?

	// Prevent the games from having trouble to reInitiate Adhoc (the next NetInit -> PdpCreate after NetTerm)
	adhocctlState = ADHOCCTL_STATE_DISCONNECTED;
	friendFinderRunning = false;

	// Log Shutdown
	INFO_LOG(Log::sceNet, "FriendFinder: End of Friend Finder Thread");

	// Return Success
	return 0;
}

int getActivePeerCount(const bool excludeTimedout) {
	// Counter
	int count = 0;

	// #ifdef LOCALHOST_AS_PEER
	// // Increase for Localhost
	// count++;
	// #endif

	// Peer Reference
	SceNetAdhocctlPeerInfo * peer = friends;

	// Iterate Peers
	for (; peer != NULL; peer = peer->next) {
		// Increase Counter, Should we exclude peers pending for timed out?
		if (!excludeTimedout || peer->last_recv != 0)
			count++;
	}

	// Return Result
	return count;
}

int getLocalIp(sockaddr_in* SocketAddress) {
	if (isLocalServer) {
		SocketAddress->sin_addr = g_localhostIP.in.sin_addr;
		return 0;
	}

#if !PPSSPP_PLATFORM(SWITCH)
	if (metasocket != (int)INVALID_SOCKET) {
		struct sockaddr_in localAddr {};
		localAddr.sin_addr.s_addr = INADDR_ANY;
		socklen_t addrLen = sizeof(localAddr);
		int ret = getsockname((int)metasocket, (struct sockaddr*)&localAddr, &addrLen);
		// Note: Sometimes metasocket still contains a valid socket fd right after failed to connect to AdhocServer on a different thread, thus ended with 0.0.0.0 here
		if (SOCKET_ERROR != ret && localAddr.sin_addr.s_addr != 0) {
			SocketAddress->sin_addr = localAddr.sin_addr;
			return 0;
		}
	}
#endif // !PPSSPP_PLATFORM(SWITCH)

// Fallback if not connected to AdhocServer
// getifaddrs first appeared in glibc 2.3, On Android officially supported since __ANDROID_API__ >= 24
#if (defined(_IFADDRS_H_) || (__GLIBC__ > 2) || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 3) || (__ANDROID_API__ >= 24))
	struct ifaddrs* ifAddrStruct = NULL;
	struct ifaddrs* ifa = NULL;

	getifaddrs(&ifAddrStruct);
	if (ifAddrStruct != NULL) {
		for (ifa = ifAddrStruct; ifa != NULL; ifa = ifa->ifa_next) {
			if (!ifa->ifa_addr) {
				continue;
			}
			if (ifa->ifa_addr->sa_family == AF_INET) { // check it is IP4
				// is a valid IP4 Address
				SocketAddress->sin_addr = ((struct sockaddr_in*)ifa->ifa_addr)->sin_addr;
				u32 addr = ((struct sockaddr_in*)ifa->ifa_addr)->sin_addr.s_addr;
				if (addr != 0x0100007f) {  // 127.0.0.1
					// Found a plausible one
					break;
				}
			}
		}
		freeifaddrs(ifAddrStruct);
		return 0;
	}

#else // Alternative way
	// Socket doesn't "leak" to the game.
	int sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock != SOCKET_ERROR) {
		const char* kGoogleDnsIp = "8.8.8.8"; // Needs to be an IP string so it can be resolved as fast as possible to IP, doesn't need to be reachable
		uint16_t kDnsPort = 53;
		struct sockaddr_in serv {};
		u32 ipv4 = INADDR_NONE; // inet_addr(kGoogleDnsIp); // deprecated?
		inet_pton(AF_INET, kGoogleDnsIp, &ipv4);
		serv.sin_family = AF_INET;
		serv.sin_addr.s_addr = ipv4;
		serv.sin_port = htons(kDnsPort);

		int err = connect(sock, (struct sockaddr*)&serv, sizeof(serv)); // connect should succeed even with SOCK_DGRAM
		if (err != SOCKET_ERROR) {
			struct sockaddr_in name {};
			socklen_t namelen = sizeof(name);
			err = getsockname(sock, (struct sockaddr*)&name, &namelen);
			if (err != SOCKET_ERROR) {
				SocketAddress->sin_addr = name.sin_addr; // May be we should cache this so it doesn't need to use connect all the time, or even better cache it when connecting to adhoc server to get an accurate IP
				closesocket(sock);
				return 0;
			}
		}
		closesocket(sock);
	}
#endif
	return -1;
}

uint32_t getLocalIp(int sock) {
	struct sockaddr_in localAddr {};
	localAddr.sin_addr.s_addr = INADDR_ANY;
	socklen_t addrLen = sizeof(localAddr);
	getsockname(sock, (struct sockaddr*)&localAddr, &addrLen);
	if (isLocalServer) {
		localAddr.sin_addr = g_localhostIP.in.sin_addr;
	}
	return localAddr.sin_addr.s_addr;
}

static std::vector<std::pair<uint32_t, uint32_t>> InitPrivateIPRanges() {
	struct sockaddr_in saNet {}, saMask{};
	std::vector<std::pair<uint32_t, uint32_t>> ip_ranges;

	if (1 == inet_pton(AF_INET, "192.168.0.0", &(saNet.sin_addr)) && 1 == inet_pton(AF_INET, "255.255.0.0", &(saMask.sin_addr)))
		ip_ranges.push_back({saNet.sin_addr.s_addr, saMask.sin_addr.s_addr});
	if (1 == inet_pton(AF_INET, "172.16.0.0", &(saNet.sin_addr)) && 1 == inet_pton(AF_INET, "255.240.0.0", &(saMask.sin_addr)))
		ip_ranges.push_back({ saNet.sin_addr.s_addr, saMask.sin_addr.s_addr });
	if (1 == inet_pton(AF_INET, "10.0.0.0", &(saNet.sin_addr)) && 1 == inet_pton(AF_INET, "255.0.0.0", &(saMask.sin_addr)))
		ip_ranges.push_back({ saNet.sin_addr.s_addr, saMask.sin_addr.s_addr });
	if (1 == inet_pton(AF_INET, "127.0.0.0", &(saNet.sin_addr)) && 1 == inet_pton(AF_INET, "255.0.0.0", &(saMask.sin_addr)))
		ip_ranges.push_back({ saNet.sin_addr.s_addr, saMask.sin_addr.s_addr });
	if (1 == inet_pton(AF_INET, "169.254.0.0", &(saNet.sin_addr)) && 1 == inet_pton(AF_INET, "255.255.0.0", &(saMask.sin_addr)))
		ip_ranges.push_back({ saNet.sin_addr.s_addr, saMask.sin_addr.s_addr });

	return ip_ranges;
}

bool isPrivateIP(uint32_t ip) {
	static const std::vector<std::pair<uint32_t, uint32_t>> ip_ranges = InitPrivateIPRanges();
	for (auto& ipRange : ip_ranges) {
		if ((ip & ipRange.second) == (ipRange.first & ipRange.second)) // We can just use ipRange.first directly if it's already correctly formatted
			return true;
	}
	return false;
}

bool isAPIPA(uint32_t ip) {
	return (((uint8_t*)&ip)[0] == 169 && ((uint8_t*)&ip)[1] == 254);
}

bool isLoopbackIP(uint32_t ip) {
	return ((uint8_t*)&ip)[0] == 0x7f;
}

bool isMulticastIP(uint32_t ip) {
	return ((ip & 0xF0) == 0xE0);
}

bool isBroadcastIP(uint32_t ip, const uint32_t subnetmask) {
	return (ip == (ip | (~subnetmask)));
}

void getLocalMac(SceNetEtherAddr * addr){
	// Read MAC Address from config
	uint8_t mac[ETHER_ADDR_LEN] = {0};
	if (PPSSPP_ID > 1) {
		memset(&mac, PPSSPP_ID, sizeof(mac));
		// Making sure the 1st 2-bits on the 1st byte of OUI are zero to prevent issue with some games (ie. Gran Turismo)
		mac[0] &= 0xfc;
	}
	else
	if (!ParseMacAddress(g_Config.sMACAddress, mac)) {
		ERROR_LOG(Log::sceNet, "Error parsing mac address %s", g_Config.sMACAddress.c_str());
		memset(&mac, 0, sizeof(mac));
	}
	memcpy(addr, mac, ETHER_ADDR_LEN);
}

uint16_t getLocalPort(int sock) {
	struct sockaddr_in localAddr {};
	localAddr.sin_port = 0;
	socklen_t addrLen = sizeof(localAddr);
	getsockname(sock, (struct sockaddr*)&localAddr, &addrLen);
	return ntohs(localAddr.sin_port);
}

u_long getAvailToRecv(int sock, int udpBufferSize) {
	u_long n = 0; // Typical MTU size is 1500
	int err = -1;
	// Note: FIONREAD may have different behavior depends on the platform, according to https://stackoverflow.com/questions/9278189/how-do-i-get-amount-of-queued-data-for-udp-socket/9296481#9296481
#if defined(_WIN32)
	err = ioctlsocket(sock, FIONREAD, &n);
#else
	err = ioctl(sock, FIONREAD, &n);
#endif
	if (err < 0)
		return 0;

	if (udpBufferSize > 0 && n > 0) {
		// TODO: May need to filter out packets from an IP that can't be translated to MAC address
		// TODO: Cap number of bytes of full DGRAM message(s) up to buffer size, but may cause Warriors Orochi 2 to get FPS drops
	}
	return n;
}

int getSockMaxSize(int udpsock) {
	int n = PSP_ADHOC_PDP_MTU; // Typical MTU size is 1500
#if defined(SO_MAX_MSG_SIZE) // May not be available on all platform
	socklen_t m = sizeof(n);
	getsockopt(udpsock, SOL_SOCKET, SO_MAX_MSG_SIZE, (char*)&n, &m);
#endif
	return n;
}

int getSockBufferSize(int sock, int opt) { // opt = SO_RCVBUF/SO_SNDBUF
	int n = PSP_ADHOC_PDP_MFS*2; // 16384; // The value might be twice of the value being set using setsockopt
	socklen_t m = sizeof(n);
	getsockopt(sock, SOL_SOCKET, opt, (char *)&n, &m);
	return (n);
}

int setSockBufferSize(int sock, int opt, int size) { // opt = SO_RCVBUF/SO_SNDBUF
	int n = size; // 8192;
	switch (opt) {
		case SO_RCVBUF: n = std::max(size, 128); break; // FIXME: The minimum (doubled) value for SO_RCVBUF is 256 ? (2048+MTU+padding on newer OS? TCP_SKB_MIN_TRUESIZE)
		case SO_SNDBUF: n = std::max(size, 1024); break; // FIXME: The minimum (doubled) value for SO_SNDBUF is 2048 ? (twice the minimum of SO_RCVBUF on newer OS? TCP_SKB_MIN_TRUESIZE * 2)
	}
	return setsockopt(sock, SOL_SOCKET, opt, (char *)&n, sizeof(n));
}

int setSockMSS(int sock, int size) {
	int mss = size; // 1460;
	return setsockopt(sock, IPPROTO_TCP, TCP_MAXSEG, (char*)&mss, sizeof(mss));
}

int setSockTimeout(int sock, int opt, unsigned long timeout_usec) { // opt = SO_SNDTIMEO/SO_RCVTIMEO
	if (timeout_usec > 0 && timeout_usec < minSocketTimeoutUS) timeout_usec = minSocketTimeoutUS; // Override timeout for high latency multiplayer
#if defined(_WIN32)
	unsigned long optval = timeout_usec / 1000UL;
	if (timeout_usec > 0 && optval == 0) optval = 1; // Since there are games that use 100 usec timeout, we should set it to minimum value on Windows (1 msec) instead of using 0 (0 = indefinitely timeout)
#elif defined(__APPLE__)
	struct timeval optval;
	optval.tv_sec = static_cast<long>(timeout_usec) / 1000000L;
	optval.tv_usec = static_cast<long>(timeout_usec) % 1000000L;
#else
	struct timeval optval = { static_cast<long>(timeout_usec) / 1000000L, static_cast<long>(timeout_usec) % 1000000L };
#endif
	return setsockopt(sock, SOL_SOCKET, opt, (char*)&optval, sizeof(optval));
}

int getSockError(int sock) {
	int result = 0;
	socklen_t result_len = sizeof(result);
	if (getsockopt(sock, SOL_SOCKET, SO_ERROR, (char*)&result, &result_len) < 0) {
		result = socket_errno;
	}
	return result;
}

int getSockNoDelay(int tcpsock) { 
	int opt = 0;
	socklen_t optlen = sizeof(opt);
	getsockopt(tcpsock, IPPROTO_TCP, TCP_NODELAY, (char*)&opt, &optlen);
	return opt;
}

//#define TCP_QUICKACK     0x0c
int setSockNoDelay(int tcpsock, int flag) {
	int opt = flag;
	// Disable ACK Delay when supported
#if defined(TCP_QUICKACK)
	setsockopt(tcpsock, IPPROTO_TCP, TCP_QUICKACK, (char*)&opt, sizeof(opt));
#elif defined(_WIN32)
#if !defined(SIO_TCP_SET_ACK_FREQUENCY)
	#define SIO_TCP_SET_ACK_FREQUENCY _WSAIOW(IOC_VENDOR,23)
#endif
	int freq = flag? 1:2; // can be 1..255, default is 2 (delayed 200ms)
	DWORD retbytes = 0;
	WSAIoctl(tcpsock, SIO_TCP_SET_ACK_FREQUENCY, &freq, sizeof(freq), NULL, 0, &retbytes, NULL, NULL);
#endif
	// Disable Nagle Algo
	return setsockopt(tcpsock, IPPROTO_TCP, TCP_NODELAY, (char*)&opt, sizeof(opt));
}

int setSockNoSIGPIPE(int sock, int flag) {
	// Set SIGPIPE when supported (ie. BSD/MacOS X)
	int opt = flag;
#if defined(SO_NOSIGPIPE)
	// Note: Linux might have SO_NOSIGPIPE defined too, but using it on setsockopt will result to EINVAL error
	return setsockopt(sock, SOL_SOCKET, SO_NOSIGPIPE, (void*)&opt, sizeof(opt));
#endif
	return -1;
}

int setSockReuseAddrPort(int sock) {
	int opt = 1;
	// Should we set SO_BROADCAST too for SO_REUSEADDR to works like SO_REUSEPORT ?
	// Set SO_REUSEPORT also when supported (ie. Android)
#if defined(SO_REUSEPORT)
	setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, (const char*)&opt, sizeof(opt));
#endif
	return setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
}

int setUDPConnReset(int udpsock, bool enabled) {
	// On Windows: Connection Reset error on UDP could cause a strange behavior https://stackoverflow.com/questions/34242622/windows-udp-sockets-recvfrom-fails-with-error-10054
#if defined(_WIN32)
#if !defined(SIO_UDP_CONNRESET)
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif
	BOOL bNewBehavior = enabled;
	DWORD dwBytesReturned = 0;
	return WSAIoctl(udpsock, SIO_UDP_CONNRESET, &bNewBehavior, sizeof bNewBehavior, NULL, 0, &dwBytesReturned, NULL, NULL);
#endif
	return -1;
}

#if !defined(TCP_KEEPIDLE) && !PPSSPP_PLATFORM(SWITCH)
#define TCP_KEEPIDLE	TCP_KEEPALIVE //TCP_KEEPIDLE on Linux is equivalent to TCP_KEEPALIVE on macOS
#endif
// VS 2017 compatibility
#if _MSC_VER
#ifndef TCP_KEEPCNT
#define TCP_KEEPCNT 16
#endif
#ifndef TCP_KEEPINTVL
#define TCP_KEEPINTVL 17
#endif
#endif
int setSockKeepAlive(int sock, bool keepalive, const int keepinvl, const int keepcnt, const int keepidle) {
	int optval = keepalive ? 1 : 0;
	int optlen = sizeof(optval);
	int result = setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, (char*)&optval, optlen);
#if !PPSSPP_PLATFORM(SWITCH) && !PPSSPP_PLATFORM(OPENBSD)
	if (result == 0 && keepalive) {
		if (getsockopt(sock, SOL_SOCKET, SO_TYPE, (char*)&optval, (socklen_t*)&optlen) == 0 && optval == SOCK_STREAM) {
			optlen = sizeof(optval);
			optval = keepidle; //180 sec
			setsockopt(sock, IPPROTO_TCP, TCP_KEEPIDLE, (char*)&optval, optlen);		
			optval = keepinvl; //60 sec
			setsockopt(sock, IPPROTO_TCP, TCP_KEEPINTVL, (char*)&optval, optlen);
			optval = keepcnt; //20
			setsockopt(sock, IPPROTO_TCP, TCP_KEEPCNT, (char*)&optval, optlen);
		}
	}
#endif // !PPSSPP_PLATFORM(SWITCH) && !PPSSPP_PLATFORM(OPENBSD)
	return result;
}

/**
* Return the Number of Players with the chosen Nickname in the Local Users current Network
* @param nickname To-be-searched Nickname
* @return Number of matching Players
*/
int getNicknameCount(const char * nickname)
{
	// Counter
	int count = 0;

	// Local Nickname Matches
	if (strncmp((char *)&parameter.nickname.data, nickname, ADHOCCTL_NICKNAME_LEN) == 0) count++;

	// Peer Reference
	SceNetAdhocctlPeerInfo * peer = friends;

	// Iterate Peers
	for (; peer != NULL; peer = peer->next)
	{
		// Match found
		if (peer->last_recv != 0 && strncmp((char *)&peer->nickname.data, nickname, ADHOCCTL_NICKNAME_LEN) == 0) count++;
	}

	// Return Result
	return count;
}

/**
* PDP Socket Counter
* @return Number of internal PDP Sockets
*/
int getPDPSocketCount()
{
	// Socket Counter
	int counter = 0;

	// Count Sockets
	for (int i = 0; i < MAX_SOCKET; i++) 
		if (adhocSockets[i] != NULL && adhocSockets[i]->type == SOCK_PDP) 
			counter++;

	// Return Socket Count
	return counter;
}

int getPTPSocketCount() {
	// Socket Counter
	int counter = 0;

	// Count Sockets
	for (int i = 0; i < MAX_SOCKET; i++)
		if (adhocSockets[i] != NULL && adhocSockets[i]->type == SOCK_PTP)
			counter++;

	// Return Socket Count
	return counter;
}

int initNetwork(SceNetAdhocctlAdhocId *adhoc_id){
	auto n = GetI18NCategory(I18NCat::NETWORKING);
	int iResult = 0;
	metasocket = (int)INVALID_SOCKET;
	metasocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (metasocket == INVALID_SOCKET){
		ERROR_LOG(Log::sceNet, "Invalid socket");
		return SOCKET_ERROR;
	}
	setSockKeepAlive((int)metasocket, true);
	// Disable Nagle Algo to prevent delaying small packets
	setSockNoDelay((int)metasocket, 1);
	// Switch to Nonblocking Behaviour
	changeBlockingMode((int)metasocket, 1);
	// Ignore SIGPIPE when supported (ie. BSD/MacOS)
	setSockNoSIGPIPE((int)metasocket, 1);

	// If Server is at localhost Try to Bind socket to specific adapter before connecting to prevent 2nd instance being recognized as already existing 127.0.0.1 by AdhocServer
	// (may not works in WinXP/2003 for IPv4 due to "Weak End System" model)
	if (isLoopbackIP(g_adhocServerIP.in.sin_addr.s_addr)) { 
		int on = 1;
		// Not sure what is this SO_DONTROUTE supposed to fix, but i do remembered there were issue related to multiple-instances without SO_DONTROUTE, but forgot how to reproduce it :(
		setsockopt((int)metasocket, SOL_SOCKET, SO_DONTROUTE, (const char*)&on, sizeof(on));
		setSockReuseAddrPort((int)metasocket);

		g_localhostIP.in.sin_port = 0;
		// Bind Local Address to Socket
		iResult = bind((int)metasocket, &g_localhostIP.addr, sizeof(g_localhostIP.addr));
		if (iResult == SOCKET_ERROR) {
			ERROR_LOG(Log::sceNet, "Bind to alternate localhost[%s] failed(%i).", ip2str(g_localhostIP.in.sin_addr).c_str(), iResult);
			g_OSD.Show(OSDType::MESSAGE_ERROR, std::string(n->T("Failed to Bind Localhost IP")) + " " + ip2str(g_localhostIP.in.sin_addr).c_str());
		}
	}
	
	// Default/Initial Network Parameters
	memset(&parameter, 0, sizeof(parameter));
	strncpy((char *)&parameter.nickname.data, g_Config.sNickName.c_str(), ADHOCCTL_NICKNAME_LEN);
	parameter.nickname.data[ADHOCCTL_NICKNAME_LEN - 1] = 0;
	parameter.channel = g_Config.iWlanAdhocChannel;
	// Assign a Valid Channel when connected to AP/Adhoc if it's Auto. JPCSP use 11 as default for Auto (Commonly for Auto: 1, 6, 11)
	if (parameter.channel == PSP_SYSTEMPARAM_ADHOC_CHANNEL_AUTOMATIC) parameter.channel = defaultWlanChannel; // Faked Active channel to default channel
	//getLocalMac(&parameter.bssid.mac_addr);
	
	// Default ProductId
	product_code.type = adhoc_id->type;
	memcpy(product_code.data, adhoc_id->data, ADHOCCTL_ADHOCID_LEN);

	// Don't need to connect if AdhocServer DNS was not resolved
	if (g_adhocServerIP.in.sin_addr.s_addr == INADDR_NONE)
		return SOCKET_ERROR;

	// Don't need to connect if AdhocServer IP is the same with this instance localhost IP and having AdhocServer disabled
	if (g_adhocServerIP.in.sin_addr.s_addr == g_localhostIP.in.sin_addr.s_addr && !g_Config.bEnableAdhocServer)
		return SOCKET_ERROR;

	// Connect to Adhoc Server
	int errorcode = 0;
	int cnt = 0;
	DEBUG_LOG(Log::sceNet, "InitNetwork: Connecting to AdhocServer");
	iResult = connect((int)metasocket, &g_adhocServerIP.addr, sizeof(g_adhocServerIP));
	errorcode = socket_errno;

	if (iResult == SOCKET_ERROR && errorcode != EISCONN) {
		u64 startTime = (u64)(time_now_d() * 1000000.0);
		bool done = false;
		while (!done) {
			if (coreState == CORE_POWERDOWN) 
				return iResult;

			done = (IsSocketReady((int)metasocket, false, true) > 0);
			struct sockaddr_in sin;
			socklen_t sinlen = sizeof(sin);
			memset(&sin, 0, sinlen);
			// Ensure that the connection really established or not, since "select" alone can't accurately detects it
			done &= (getpeername((int)metasocket, (struct sockaddr*)&sin, &sinlen) != SOCKET_ERROR);
			u64 now = (u64)(time_now_d() * 1000000.0);
			if (static_cast<s64>(now - startTime) > adhocDefaultTimeout) {
				if (connectInProgress(errorcode))
					errorcode = ETIMEDOUT;
				break;
			}
			sleep_ms(10, "pro-adhoc-socket-poll");
		}
		if (!done) {
			ERROR_LOG(Log::sceNet, "Socket error (%i) when connecting to AdhocServer [%s/%s:%u]", errorcode, g_Config.proAdhocServer.c_str(), ip2str(g_adhocServerIP.in.sin_addr).c_str(), ntohs(g_adhocServerIP.in.sin_port));
			g_OSD.Show(OSDType::MESSAGE_ERROR, std::string(n->T("Failed to connect to Adhoc Server")) + " (" + std::string(n->T("Error")) + ": " + std::to_string(errorcode) + ")");
			return iResult;
		}
	}

	// Prepare Login Packet
	SceNetAdhocctlLoginPacketC2S packet;
	packet.base.opcode = OPCODE_LOGIN;
	SceNetEtherAddr addres;
	getLocalMac(&addres);
	packet.mac = addres;
	strncpy((char *)&packet.name.data, g_Config.sNickName.c_str(), ADHOCCTL_NICKNAME_LEN);
	packet.name.data[ADHOCCTL_NICKNAME_LEN - 1] = 0;
	memcpy(packet.game.data, adhoc_id->data, ADHOCCTL_ADHOCID_LEN);

	IsSocketReady((int)metasocket, false, true, nullptr, adhocDefaultTimeout);
	DEBUG_LOG(Log::sceNet, "InitNetwork: Sending LOGIN OPCODE %d", packet.base.opcode);
	int sent = (int)send((int)metasocket, (char*)&packet, sizeof(packet), MSG_NOSIGNAL);
	if (sent > 0) {
		socklen_t addrLen = sizeof(LocalIP);
		memset(&LocalIP, 0, addrLen);
		getsockname((int)metasocket, &LocalIP, &addrLen);
		return 0;
	} else {
		return SOCKET_ERROR;
	}
}

bool isZeroMAC(const SceNetEtherAddr* addr) {
	return (memcmp(addr->data, "\x00\x00\x00\x00\x00\x00", ETHER_ADDR_LEN) == 0);
}

bool isBroadcastMAC(const SceNetEtherAddr * addr) {
	return (memcmp(addr->data, "\xFF\xFF\xFF\xFF\xFF\xFF", ETHER_ADDR_LEN) == 0);
}

bool resolveIP(uint32_t ip, SceNetEtherAddr * mac) {
	sockaddr_in addr;
	getLocalIp(&addr);
	uint32_t localIp = addr.sin_addr.s_addr;

	if (ip == localIp || ip == g_localhostIP.in.sin_addr.s_addr) {
		getLocalMac(mac);
		return true;
	}

	// Multithreading Lock
	std::lock_guard<std::recursive_mutex> peer_guard(peerlock);

	// Peer Reference
	SceNetAdhocctlPeerInfo * peer = friends;

	// Iterate Peers
	for (; peer != NULL; peer = peer->next) {
		// Found Matching Peer
		if (peer->ip_addr == ip) {
			// Copy Data
			*mac = peer->mac_addr;

			// Return Success
			return true;
		}
	}

	// Peer not found
	return false;
}

bool resolveMAC(SceNetEtherAddr* mac, uint32_t* ip, u16* port_offset) {
	// Get Local MAC Address
	SceNetEtherAddr localMac;
	getLocalMac(&localMac);
	// Local MAC Requested
	if (isMacMatch(&localMac, mac)) {
		// Get Local IP Address
		sockaddr_in sockAddr;
		getLocalIp(&sockAddr);
		*ip = sockAddr.sin_addr.s_addr;
		if (port_offset)
			*port_offset = portOffset;
		return true; // return succes
	}

	// Multithreading Lock
	std::lock_guard<std::recursive_mutex> peer_guard(peerlock);

	// Peer Reference
	SceNetAdhocctlPeerInfo * peer = friends;

	// Iterate Peers
	for (; peer != NULL; peer = peer->next) {
		// Found Matching Peer
		if (isMacMatch(&peer->mac_addr, mac)) {
			// Copy Data
			*ip = peer->ip_addr;
			if (port_offset)
				*port_offset = peer->port_offset;
			// Return Success
			return true;
		}
	}

	// Peer not found
	return false;
}

bool validNetworkName(const char *data) {
	// Result
	bool valid = true;

	// Name given
	if (data != NULL) {
		// Iterate Name Characters
		for (int i = 0; i < ADHOCCTL_GROUPNAME_LEN && valid; i++) {
			// End of Name
			if (data[i] == 0) break;

			// Not a digit
			if (data[i] < '0' || data[i] > '9') {
				// Not 'A' to 'Z'
				if (data[i] < 'A' || data[i] > 'Z') {
					// Not 'a' to 'z'
					if (data[i] < 'a' || data[i] > 'z') {
						// Invalid Name
						valid = false;
					}
				}
			}
		}
	}
	// Return Result
	return valid;
}

u64 join32(u32 num1, u32 num2){
	return (u64)num2 << 32 | num1;
}

void split64(u64 num, int buff[]){
	int num1 = (int)(num&firstMask);
	int num2 = (int)((num&secondMask) >> 32);
	buff[0] = num1;
	buff[1] = num2;
}

const char* getMatchingEventStr(int code) {
	const char *buf = NULL;
	switch (code) {
	case PSP_ADHOC_MATCHING_EVENT_HELLO:
		buf = "HELLO"; break;
	case PSP_ADHOC_MATCHING_EVENT_REQUEST:
		buf = "JOIN"; break;
	case PSP_ADHOC_MATCHING_EVENT_LEAVE:
		buf = "LEAVE"; break;
	case PSP_ADHOC_MATCHING_EVENT_DENY:
		buf = "REJECT"; break;
	case PSP_ADHOC_MATCHING_EVENT_CANCEL:
		buf = "CANCEL"; break;
	case PSP_ADHOC_MATCHING_EVENT_ACCEPT:
		buf = "ACCEPT"; break;
	case PSP_ADHOC_MATCHING_EVENT_ESTABLISHED:
		buf = "ESTABLISHED"; break;
	case PSP_ADHOC_MATCHING_EVENT_TIMEOUT:
		buf = "TIMEOUT"; break;
	case PSP_ADHOC_MATCHING_EVENT_ERROR:
		buf = "ERROR"; break;
	case PSP_ADHOC_MATCHING_EVENT_BYE:
		buf = "DISCONNECT"; break;
	case PSP_ADHOC_MATCHING_EVENT_DATA:
		buf = "DATA"; break;
	case PSP_ADHOC_MATCHING_EVENT_DATA_ACK:
		buf = "DATA_ACK"; break;
	case PSP_ADHOC_MATCHING_EVENT_DATA_TIMEOUT:
		buf = "DATA_TIMEOUT"; break;
	case PSP_ADHOC_MATCHING_EVENT_INTERNAL_PING:
		buf = "INTERNAL_PING"; break;
	default:
		buf = "UNKNOWN";
	}
	return buf;
}

const char* getMatchingOpcodeStr(int code) {
	const char *buf = NULL;
	switch (code) {
	case PSP_ADHOC_MATCHING_PACKET_PING:
		buf = "PING"; break;
	case PSP_ADHOC_MATCHING_PACKET_HELLO:
		buf = "HELLO"; break;
	case PSP_ADHOC_MATCHING_PACKET_JOIN:
		buf = "JOIN"; break;
	case PSP_ADHOC_MATCHING_PACKET_ACCEPT:
		buf = "ACCEPT"; break;
	case PSP_ADHOC_MATCHING_PACKET_CANCEL:
		buf = "CANCEL"; break;
	case PSP_ADHOC_MATCHING_PACKET_BULK:
		buf = "BULK"; break;
	case PSP_ADHOC_MATCHING_PACKET_BULK_ABORT:
		buf = "BULK_ABORT"; break;
	case PSP_ADHOC_MATCHING_PACKET_BIRTH:
		buf = "BIRTH"; break;
	case PSP_ADHOC_MATCHING_PACKET_DEATH:
		buf = "DEATH"; break;
	case PSP_ADHOC_MATCHING_PACKET_BYE:
		buf = "BYE"; break;
	default:
		buf = "UNKNOWN";
	}
	return buf;
}

const char *AdhocCtlStateToString(int state) {
	switch (state) {
	case ADHOCCTL_STATE_DISCONNECTED: return "DISCONNECTED";
	case ADHOCCTL_STATE_CONNECTED: return "CONNECTED";
	case ADHOCCTL_STATE_SCANNING: return "SCANNING";
	case ADHOCCTL_STATE_GAMEMODE: return "GAMEMODE";
	case ADHOCCTL_STATE_DISCOVER: return "DISCOVER";
	case ADHOCCTL_STATE_WOL: return "WOL";
	default: return "(unk)";
	}
}
